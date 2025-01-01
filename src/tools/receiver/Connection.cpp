#include "Connection.hpp"

#include <base/Log.hpp>
#include <base/io/TerminalColors.hpp>

#include <algorithm>
#include <filesystem>

#define TERMINAL_COLOR_IP(x) TERMINAL_COLOR_GREEN(x)

namespace receiver {

void Connection::cleanup() {
  if (download) {
    const auto path = download->fs_path;
    download = {};

    std::error_code ec;
    std::filesystem::remove(path, ec);

    log_error("{}: removing `{}`", peer_address, path);
  }
}

void Connection::on_error(ErrorType type, sock::Status status) {
  cleanup();
  log_error("{}: error - {}", peer_address, status.stringify());
}
void Connection::on_protocol_error(std::string_view description) {
  cleanup();
  log_error("{}: error - {}", peer_address, description);
}
void Connection::on_disconnected() {
  cleanup();
  if (state != State::Idle) {
    log_error("{}: disconnected unexpectedly", peer_address);
  } else {
    log_info(TERMINAL_COLOR_IP("{}") ": disconnected", peer_address);
  }
}

bool Connection::to_fs_path(std::string_view virtual_path, std::string& fs_path) {
  if (virtual_path.find("::") != std::string_view::npos) {
    protocol_error(base::format("path `{}` contains `..`", virtual_path));
    return false;
  }

  fs_path = receive_directory + "/" + std::string(virtual_path);
  return true;
}

bool Connection::create_directory(std::string_view virtual_path) {
  std::string fs_path;
  if (!to_fs_path(virtual_path, fs_path)) {
    protocol_error(base::format("failed to convert `{}` to filesystem path", virtual_path));
    return false;
  }

  if (std::filesystem::exists(fs_path) && std::filesystem::is_directory(fs_path)) {
    return true;
  }

  std::error_code ec;
  std::filesystem::create_directories(fs_path, ec);
  if (ec == std::errc{}) {
    log_info(TERMINAL_COLOR_IP("{}") ": created directory `{}`", peer_address, virtual_path);
    return true;
  } else {
    protocol_error(base::format("failed to create directory `{}`", fs_path));
    return false;
  }
}

bool Connection::start_file_download(std::string_view virtual_path,
                                     uint64_t file_size,
                                     uint16_t flags) {
  std::string fs_path;
  if (!to_fs_path(virtual_path, fs_path)) {
    protocol_error(base::format("failed to convert `{}` to filesystem path", virtual_path));
    return false;
  }

  if (std::filesystem::exists(fs_path)) {
    protocol_error(base::format("path `{}` already exists", fs_path));
    return false;
  }

  base::File file{fs_path, "wb", base::File::OpenFlags::NoBuffering};
  if (!file) {
    protocol_error(base::format("failed to open file `{}` for writing", fs_path));
    return false;
  }

  const bool is_compressed = (flags & net::packets::CreateFile::flag_compressed) != 0;
  if (is_compressed) {
    ZSTD_DCtx_reset(decompression_context, ZSTD_reset_session_only);
  }

  state = State::Downloading;
  download = Download{
    .file = std::move(file),
    .virtual_path = std::string(virtual_path),
    .fs_path = fs_path,
    .file_size = file_size,
    .downloaded_size = 0,
    .is_compressed = is_compressed,
  };

  download_hasher.reset();
  download_tracker.begin(std::string(virtual_path), file_size, is_compressed);

  if (file_size == 0) {
    finish_chunks_download();
  }

  return true;
}

void Connection::process_downloaded_chunk(std::span<const uint8_t> download_chunk) {
  auto& down = *download;

  std::span<const uint8_t> file_chunk = download_chunk;

  if (down.is_compressed) {
    decompression_buffer.clear();

    const auto base_step_size = std::max(size_t(4096), file_chunk.size());

    ZSTD_inBuffer input{download_chunk.data(), download_chunk.size(), 0};
    while (input.pos < input.size) {
      const auto current_step_size =
        std::max(decompression_buffer.unused_capacity(), base_step_size);

      const auto previous_size = decompression_buffer.size();
      decompression_buffer.resize(previous_size + current_step_size);

      ZSTD_outBuffer output{decompression_buffer.data() + previous_size, current_step_size, 0};

      if (ZSTD_isError(ZSTD_decompressStream(decompression_context, &output, &input))) {
        return protocol_error("failed to decompress the file chunk");
      }

      decompression_buffer.resize(previous_size + output.pos);
    }

    file_chunk = decompression_buffer.span();
  }

  if (down.file.write(file_chunk) != file_chunk.size()) {
    return protocol_error(base::format("failed to write to file `{}`", down.fs_path));
  }

  down.downloaded_size += file_chunk.size();
  if (down.downloaded_size > down.file_size) {
    return protocol_error(
      base::format("got more file data for `{}` than expected", down.virtual_path));
  }

  download_hasher.feed(file_chunk);
  download_tracker.progress(file_chunk.size(), download_chunk.size());

  if (down.downloaded_size == down.file_size) {
    finish_chunks_download();
  }
}

void Connection::finish_chunks_download() {
  download_tracker.end();
  state = State::WaitingForHash;
}

bool Connection::verify_file(uint64_t hash) {
  auto& down = *download;

  const auto downloaded_hash = download_hasher.finalize();
  if (hash != downloaded_hash) {
    protocol_error(base::format("file `{}` failed the integrity check", down.virtual_path));
    return false;
  }

  state = State::Idle;
  download = {};

  return true;
}

void Connection::on_packet_received(const net::packets::ReceiverHello& packet) {
  protocol_error("received unexpected ReceiverHello packet");
}

void Connection::on_packet_received(const net::packets::SenderHello& packet) {
  if (state == State::WaitingForHello) {
    send_packet(net::packets::ReceiverHello{});
    state = State::Idle;
  } else {
    protocol_error("received unxpected SenderHello packet");
  }
}

void Connection::on_packet_received(const net::packets::Acknowledged& packet) {
  protocol_error("received unexpected Acknowledged packet");
}

void Connection::on_packet_received(const net::packets::CreateDirectory& packet) {
  if (state == State::Idle) {
    const auto created = create_directory(packet.path);
    send_packet(net::packets::Acknowledged{
      .accepted = created,
    });
  } else {
    protocol_error("received unexpected CreateDirectory packet");
  }
}

void Connection::on_packet_received(const net::packets::CreateFile& packet) {
  if (state == State::Idle) {
    const auto started = start_file_download(packet.path, packet.size, packet.flags);
    send_packet(net::packets::Acknowledged{
      .accepted = started,
    });
  } else {
    protocol_error("received unexpected CreateFile packet");
  }
}

void Connection::on_packet_received(const net::packets::FileChunk& packet) {
  if (state == State::Downloading) {
    process_downloaded_chunk(packet.data);
  } else {
    protocol_error("received unexpected CreateFile packet");
  }
}

void Connection::on_packet_received(const net::packets::VerifyFile& packet) {
  if (state == State::WaitingForHash) {
    const auto verified = verify_file(packet.hash);
    send_packet(net::packets::Acknowledged{
      .accepted = verified,
    });
  } else {
    protocol_error("received unxpected Acknowledged packet");
  }
}

Connection::Connection(sock::StreamSocket socket,
                       std::string peer_address,
                       std::string receive_directory)
    : net::ProtocolConnection(std::move(socket)),
      peer_address(std::move(peer_address)),
      receive_directory(std::move(receive_directory)),
      decompression_context(ZSTD_createDCtx()),
      download_tracker("downloading", [this](std::string_view message) {
        log_info(TERMINAL_COLOR_IP("{}") ": {}", this->peer_address, message);
      }) {}

Connection::~Connection() {
  ZSTD_freeDCtx(decompression_context);
}

}  // namespace receiver