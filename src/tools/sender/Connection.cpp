#include "Connection.hpp"

#include <base/Log.hpp>
#include <base/Panic.hpp>

#include <algorithm>

namespace sender {

constexpr static size_t max_chunk_size = 128 * 1024;
constexpr static size_t max_compressed_chunk_size = 64 * 1024;

void Connection::on_error(ErrorType type, sock::Status status) {
  log_error("error - {}", status.stringify());
}
void Connection::on_protocol_error(std::string_view description) {
  log_error("error - {}", description);
}
void Connection::on_disconnected() {
  if (state == State::Finished) {
    log_info("disconnected");
  } else {
    log_error("disconnected unexpectedly");
  }
}

bool Connection::should_compress_file(std::string_view fs_path, size_t size) const {
  return false;
}

void Connection::create_directory(std::string_view virtual_path) {
  log_info("creating directory `{}`...", virtual_path);

  if (send_packet(net::packets::CreateDirectory{.path = virtual_path})) {
    state = State::WaitingForDirectoryCreationAcknowledgement;
  }
}

void Connection::start_file_upload(std::string_view virtual_path, const std::string& fs_path) {
  base::File file{fs_path, "rb", base::File::OpenFlags::NoBuffering};
  if (!file) {
    return protocol_error(base::format("failed to open file `{}` for reading", fs_path));
  }

  file.seek(base::File::SeekOrigin::End, 0);
  const auto total_file_size = file.tell();
  verify(total_file_size >= 0, "total file size is negative");
  file.seek(base::File::SeekOrigin::Set, 0);

  const bool compress_file = should_compress_file(fs_path, uint64_t(total_file_size));

  uint16_t flags = 0;
  if (compress_file) {
    flags |= net::packets::CreateFile::flag_compressed;
  }

  if (!send_packet(net::packets::CreateFile{
        .path = virtual_path, .size = uint64_t(total_file_size), .flags = flags})) {
    return;
  }

  state = State::WaitingForFileCreationAcknowledgement;
  upload = Upload{
    .file = std::move(file),
    .virtual_path = std::string(virtual_path),
    .fs_path = fs_path,
    .file_size = uint64_t(total_file_size),
    .is_compressed = compress_file,
  };

  upload_tracker.begin(std::string(upload->virtual_path), upload->file_size, compress_file);
}

void Connection::upload_accepted_file() {
  auto& up = *upload;

  upload_hasher.reset();

  if (upload->is_compressed) {
    ZSTD_CCtx_reset(compression_context, ZSTD_reset_session_only);
    compression_buffer.clear();
  }

  size_t pending_uncompressed_size = 0;

  const auto flush_compression_buffer = [&]() {
    if (!compression_buffer.empty()) {
      if (!send_packet(net::packets::FileChunk{.data = compression_buffer.span()})) {
        return false;
      }

      upload_tracker.progress(pending_uncompressed_size, compression_buffer.size());

      compression_buffer.clear();
      pending_uncompressed_size = 0;
    }

    return true;
  };

  uint64_t total_bytes_read = 0;
  while (total_bytes_read < up.file_size) {
    const auto read_size = up.file.read(chunk_buffer.span());
    total_bytes_read += read_size;

    if (read_size < chunk_buffer.size() && total_bytes_read != up.file_size) {
      return protocol_error(base::format("failed to read file: `{}`", up.fs_path));
    }

    const auto file_chunk = chunk_buffer.span().subspan(0, read_size);

    if (!upload->is_compressed) {
      if (!send_packet(net::packets::FileChunk{.data = file_chunk})) {
        return;
      }

      upload_tracker.progress(file_chunk.size(), file_chunk.size());
    } else {
      const auto is_last_chunk = total_bytes_read == up.file_size;
      const auto mode = is_last_chunk ? ZSTD_e_end : ZSTD_e_continue;

      const auto base_step_size = std::max(size_t(4096), file_chunk.size());

      ZSTD_inBuffer input{file_chunk.data(), file_chunk.size(), 0};
      while (true) {
        const auto current_step_size =
          std::max(compression_buffer.unused_capacity(), base_step_size);

        const auto previous_size = compression_buffer.size();
        compression_buffer.resize(previous_size + current_step_size);

        ZSTD_outBuffer output{compression_buffer.data() + previous_size, current_step_size, 0};

        const size_t remaining = ZSTD_compressStream2(compression_context, &output, &input, mode);
        if (ZSTD_isError(remaining)) {
          return protocol_error("failed to compress the file chunk");
        }

        compression_buffer.resize(previous_size + output.pos);

        const auto finished = is_last_chunk ? (remaining == 0) : (input.pos == input.size);
        if (finished) {
          break;
        }
      }

      if (input.pos != file_chunk.size()) {
        return protocol_error("failed to compress the whole file chunk");
      }

      pending_uncompressed_size += file_chunk.size();

      if (compression_buffer.size() >= max_compressed_chunk_size) {
        if (!flush_compression_buffer()) {
          return;
        }
      }
    }

    upload_hasher.feed(file_chunk);
  }

  if (up.is_compressed) {
    if (!flush_compression_buffer()) {
      return;
    }
  }

  const auto hash = upload_hasher.finalize();

  if (!send_packet(net::packets::VerifyFile{.hash = hash})) {
    return;
  }

  upload_tracker.end();

  state = State::WaitingForUploadAcknowledgement;
  upload = {};
}

void Connection::process_send_entry(size_t index) {
  if (index >= send_entries.size()) {
    state = State::Finished;
    set_not_alive();
  } else {
    const auto& entry = send_entries[index];
    if (entry.type == FileListing::Type::Directory) {
      create_directory(entry.relative_path);
    } else {
      start_file_upload(entry.relative_path, entry.absolute_path);
    }
  }
}

void Connection::process_first_send_entry() {
  process_send_entry(current_send_entry);
}

void Connection::process_next_send_entry() {
  current_send_entry++;
  process_send_entry(current_send_entry);
}

void Connection::on_handshake_finished() {
  process_first_send_entry();
}
void Connection::on_directory_creation_accepted() {
  state = State::Idle;
  process_next_send_entry();
}
void Connection::on_file_creation_accepted() {
  upload_accepted_file();
}
void Connection::on_upload_accepted() {
  state = State::Idle;
  process_next_send_entry();
}

void Connection::on_packet_received(const net::packets::ReceiverHello& packet) {
  if (state == State::WaitingForHello) {
    state = State::Idle;
    on_handshake_finished();
  } else {
    protocol_error("received unexpected ReceiverHello packet");
  }
}

void Connection::on_packet_received(const net::packets::SenderHello& packet) {
  protocol_error("received unexpected SenderHello packet");
}

void Connection::on_packet_received(const net::packets::Acknowledged& packet) {
  switch (state) {
    case State::WaitingForDirectoryCreationAcknowledgement: {
      if (packet.accepted) {
        on_directory_creation_accepted();
      } else {
        protocol_error("server creation the directory creation request");
      }
      break;
    }

    case State::WaitingForFileCreationAcknowledgement: {
      if (packet.accepted) {
        on_file_creation_accepted();
      } else {
        protocol_error("server rejected the file creation request");
      }
      break;
    }

    case State::WaitingForUploadAcknowledgement: {
      if (packet.accepted) {
        on_upload_accepted();
      } else {
        protocol_error("server rejected the file upload");
      }
      break;
    }

    default: {
      protocol_error("received unexpected Acknowledged packet");
      break;
    }
  }
}

void Connection::on_packet_received(const net::packets::CreateDirectory& packet) {
  protocol_error("received unexpected CreateDirectory packet");
}
void Connection::on_packet_received(const net::packets::CreateFile& packet) {
  protocol_error("received unexpected CreateFile packet");
}
void Connection::on_packet_received(const net::packets::FileChunk& packet) {
  protocol_error("received unexpected FileChunk packet");
}
void Connection::on_packet_received(const net::packets::VerifyFile& packet) {
  protocol_error("received unexpected VerifyFile packet");
}

Connection::Connection(sock::StreamSocket socket, std::vector<FileListing::Entry> send_entries)
    : net::ProtocolConnection(std::move(socket)),
      send_entries(std::move(send_entries)),
      compression_context(ZSTD_createCCtx()),
      upload_tracker("uploading", [this](std::string_view message) { log_info("{}", message); }) {
  chunk_buffer.resize(max_chunk_size);
}

Connection::~Connection() {
  ZSTD_freeCCtx(compression_context);
}

void Connection::start() {
  send_packet(net::packets::SenderHello{});
}

bool Connection::finished() const {
  return state == State::Finished;
}

}  // namespace sender