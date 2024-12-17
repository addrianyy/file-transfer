#include "Connection.hpp"

#include <base/Log.hpp>

#include <helpers/IpAddressFormatter.hpp>
#include <helpers/SizeFormatter.hpp>

#include <algorithm>
#include <filesystem>

namespace receiver {

void Connection::on_error(ErrorType type, sock::Status status) {
  log_warn("{}: error - {}", peer_address, status.stringify());
}
void Connection::on_protocol_error(std::string_view description) {
  log_warn("{}: error - {}", peer_address, description);
}
void Connection::on_disconnected() {
  if (state != State::Idle) {
    log_warn("{}: disconnected unexpectedly", peer_address);
  } else {
    log_info("{}: disconnected", peer_address);
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
    log_info("{}: created directory `{}`", peer_address, virtual_path);
    return true;
  } else {
    protocol_error(base::format("failed to create directory `{}`", fs_path));
    return false;
  }
}

bool Connection::start_file_download(std::string_view virtual_path, uint64_t file_size) {
  std::string fs_path;
  if (!to_fs_path(virtual_path, fs_path)) {
    protocol_error(base::format("failed to convert `{}` to filesystem path", virtual_path));
    return false;
  }

  if (std::filesystem::exists(fs_path)) {
    protocol_error(base::format("path `{}` already exists", fs_path));
    return false;
  }

  base::File file{fs_path, "wb"};
  if (!file) {
    protocol_error(base::format("failed to open file `{}` for writing", fs_path));
    return false;
  }

  {
    const auto [readable_size, units] = SizeFormatter::bytes_to_readable_units(file_size);
    log_info("{}: downloading file `{}` ({:.2f} {})...", peer_address, virtual_path, readable_size,
             units);
  }

  const auto now = base::PreciseTime::now();

  state = State::Downloading;
  download = Download{
    .file = std::move(file),
    .virtual_path = std::string(virtual_path),
    .fs_path = fs_path,
    .file_size = file_size,
    .start_time = now,
    .last_report_time = now,
  };

  if (file_size == 0) {
    finish_file_download();
  }

  return true;
}

void Connection::process_downloaded_chunk(std::span<const uint8_t> chunk) {
  auto& down = *download;

  if (down.file.write(chunk.data(), chunk.size()) != chunk.size()) {
    return protocol_error(base::format("failed to write to file `{}`", down.fs_path));
  }

  down.downloaded_size += chunk.size();
  if (down.downloaded_size > down.file_size) {
    return protocol_error(
      base::format("got more file data for `{}` than expected", down.virtual_path));
  }

  const auto now = base::PreciseTime::now();
  if (now - down.last_report_time >= base::PreciseTime::from_seconds(1.f)) {
    const auto [downloaded_size, downloaded_size_units] =
      SizeFormatter::bytes_to_readable_units(down.downloaded_size);
    const auto [total_size, total_size_units] =
      SizeFormatter::bytes_to_readable_units(down.file_size);

    const auto download_speed = float(down.file_size) / (now - down.start_time).seconds();
    const auto [readable_speed, speed_units] =
      SizeFormatter::bytes_to_readable_units(uint64_t(download_speed));

    log_info("{}: `{}`: {:.1f}% - {:.2f}{}/{:.2f}{} - {:.2f} {}/s", peer_address, down.virtual_path,
             (float(down.downloaded_size) / float(down.file_size)) * 100.f, downloaded_size,
             downloaded_size_units, total_size, total_size_units, readable_speed, speed_units);

    down.last_report_time = now;
  }

  if (down.downloaded_size == down.file_size) {
    finish_file_download();
  }
}

void Connection::finish_file_download() {
  auto& down = *download;

  const auto download_time = base::PreciseTime::now() - down.start_time;
  const auto download_speed = float(down.file_size) / std::max(download_time.seconds(), 0.00001);

  const auto [readable_size, size_units] = SizeFormatter::bytes_to_readable_units(down.file_size);
  const auto [readable_speed, speed_units] =
    SizeFormatter::bytes_to_readable_units(uint64_t(download_speed));

  log_info("{}: downloaded file `{}` ({:.2f} {}) in {} ({:.2f} {}/s)", peer_address,
           down.virtual_path, readable_size, size_units, download_time, readable_speed,
           speed_units);

  send_packet(net::packets::Acknowledged{
    .accepted = true,
  });
  state = State::WaitingForDownloadAck;
  download = std::nullopt;
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
  if (state == State::WaitingForDownloadAck) {
    if (packet.accepted) {
      state = State::Idle;
    } else {
      protocol_error("download ccknowledged packet with accepted = false");
    }
  } else {
    protocol_error("received unxpected Acknowledged packet");
  }
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
    const auto started = start_file_download(packet.path, packet.size);
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

Connection::Connection(std::unique_ptr<sock::SocketStream> socket,
                       const sock::IpV6Address& peer_address,
                       std::string receive_directory)
    : net::ProtocolConnection(std::move(socket)),
      peer_address(IpAddressFormatter::format(peer_address)),
      receive_directory(std::move(receive_directory)) {}

}  // namespace receiver