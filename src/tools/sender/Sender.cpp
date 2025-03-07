#include "Sender.hpp"
#include "CompressionEnv.hpp"
#include "Connection.hpp"

#include <socklib/Socket.hpp>

#include <tools/Ip.hpp>

#include <net/Port.hpp>

#include <base/Log.hpp>
#include <base/text/Helpers.hpp>

bool tools::sender::run(std::span<const std::string_view> args) {
  if (args.size() < 2) {
    log_error("usage: ft send [address] [file1] [file2] ...");
    return false;
  }

  const std::string_view full_address = args[0];
  std::string_view address = full_address;
  uint16_t port = net::default_port;

  if (const auto colon = address.find(':'); colon != std::string_view::npos) {
    const auto port_string = address.substr(colon + 1);
    address = address.substr(0, colon);

    if (!base::text::to_number(port_string, port)) {
      log_error("invalid port `{}`", port_string);
      return false;
    }
  }

  FileListing listing;
  for (size_t i = 1; i < args.size(); ++i) {
    listing.add(std::string(args[i]));
  }

  auto send_entries = listing.finalize();
  if (send_entries.empty()) {
    log_error("no files to send");
    return false;
  }
  log_info("number of entries to send: {}", send_entries.size());

  log_info("compression is {} (set `FT_DISABLE_COMPRESSION` to change it)",
           CompressionEnv::is_compression_enabled() ? "enabled" : "disabled");

  auto [connect_status, connection_socket] =
    sock::StreamSocket::connect(tools::IpAddress::Version, address, port);
  if (!connect_status) {
    log_error("failed to connect to `{}`: {}", full_address, connect_status.stringify());
    return false;
  }

  log_info("connected to the receiver");

  ::sender::Connection connection{std::move(connection_socket), std::move(send_entries)};

  connection.start();

  while (connection.alive()) {
    connection.update();
  }

  return connection.finished();
}