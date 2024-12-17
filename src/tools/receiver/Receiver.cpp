#include "Receiver.hpp"
#include "Connection.hpp"

#include <socklib/Socket.hpp>

#include <helpers/IpAddressFormatter.hpp>
#include <net/Port.hpp>

#include <base/Log.hpp>
#include <base/text/Parsing.hpp>

#include <filesystem>
#include <thread>

bool tools::reciever::run(std::span<const std::string_view> args) {
  if (args.size() != 0 && args.size() != 1) {
    log_error("usage: ft receive [port]");
    return false;
  }

  uint16_t port = net::default_port;

  if (args.size() == 1) {
    if (!base::parse_integer(args[0], port)) {
      log_error("invalid port `{}`", args[0]);
      return false;
    }
  }

  const std::string receive_directory = "received";
  if (!std::filesystem::exists(receive_directory)) {
    std::error_code ec{};
    std::filesystem::create_directories(receive_directory, ec);
    if (ec != std::error_code{}) {
      log_error("failed to create directory `{}`", receive_directory);
      return false;
    }
  }

  const auto [bind_status, listener] =
    sock::Listener::bind(sock::SocketIpV6Address(sock::IpV6Address::unspecified(), port));
  if (!bind_status) {
    log_error("failed to bind receiver to port {}: {}", port, bind_status.stringify());
    return false;
  }

  log_info("listening at port {}...", port);

  while (true) {
    sock::SocketIpV6Address peer_address;

    auto [accept_status, connection_socket] = listener->accept(&peer_address);
    if (!accept_status) {
      log_warn("failed to accept client {}: {}", IpAddressFormatter::format(peer_address.ip()),
               accept_status.stringify());
      continue;
    }

    log_info("client {} connected", IpAddressFormatter::format(peer_address.ip()));

    std::thread{[connection_socket = std::move(connection_socket), peer_address,
                 receive_directory]() mutable {
      ::receiver::Connection connection{std::move(connection_socket), peer_address.ip(),
                                        receive_directory};

      while (connection.alive()) {
        connection.update();
      }
    }}.detach();
  }
}