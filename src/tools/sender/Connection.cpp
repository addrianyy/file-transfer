#include "Connection.hpp"

#include <base/Log.hpp>
#include <base/Panic.hpp>

namespace sender {

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

  if (!send_packet(
        net::packets::CreateFile{.path = virtual_path, .size = uint64_t(total_file_size)})) {
    return;
  }

  state = State::WaitingForFileCreationAcknowledgement;
  upload = Upload{
    .file = std::move(file),
    .virtual_path = std::string(virtual_path),
    .fs_path = fs_path,
    .file_size = uint64_t(total_file_size),
  };

  upload_tracker.begin(std::string(upload->virtual_path), upload->file_size);
}

void Connection::upload_accepted_file() {
  auto& up = *upload;

  upload_hasher.reset();

  uint64_t total_bytes_read = 0;
  while (total_bytes_read < up.file_size) {
    const auto read_size = up.file.read(chunk_buffer);
    total_bytes_read += read_size;

    if (read_size < chunk_buffer.size() && total_bytes_read != up.file_size) {
      return protocol_error(base::format("failed to read file: `{}`", up.fs_path));
    }

    const auto chunk = std::span(chunk_buffer).subspan(0, read_size);
    if (!send_packet(net::packets::FileChunk{.data = chunk})) {
      return;
    }

    upload_hasher.feed(chunk);

    upload_tracker.progress(chunk.size());
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

Connection::Connection(sock::SocketStream socket, std::vector<FileListing::Entry> send_entries)
    : net::ProtocolConnection(std::move(socket)),
      send_entries(std::move(send_entries)),
      upload_tracker("uploading", [this](std::string_view message) { log_info("{}", message); }) {
  chunk_buffer.resize(128 * 1024);
}

void Connection::start() {
  send_packet(net::packets::SenderHello{});
}

bool Connection::finished() const {
  return state == State::Finished;
}

}  // namespace sender