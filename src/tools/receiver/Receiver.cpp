#include "Receiver.hpp"
#include "Connection.hpp"

#include <socklib/Socket.hpp>

#include <tools/Ip.hpp>

#include <helpers/IpAddressFormatter.hpp>
#include <net/Port.hpp>

#include <base/Log.hpp>
#include <base/text/Parsing.hpp>

#include <fmt/chrono.h>

#include <ctime>
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

  const auto [bind_status, listener] =
    sock::Listener::bind(tools::SocketIpAddress(tools::IpAddress::unspecified(), port));
  if (!bind_status) {
    log_error("failed to bind receiver to port {}: {}", port, bind_status.stringify());
    return false;
  }

  log_info("listening at port {}...", port);

  std::string receive_directory;

#if 1
  {
    const std::time_t t = std::time(nullptr);
    receive_directory = base::format("receive_{:%d.%m.%Y_%H:%M:%S}", fmt::localtime(t));
  }
#else
  {
    receive_directory = "received";
  }
#endif

  if (!std::filesystem::exists(receive_directory)) {
    std::error_code ec{};
    std::filesystem::create_directories(receive_directory, ec);
    if (ec != std::error_code{}) {
      log_error("failed to create directory `{}`", receive_directory);
      return false;
    }
  }

  log_info("receiving to `{}`", receive_directory);

  while (true) {
    tools::SocketIpAddress peer_address;

    auto [accept_status, connection_socket] = listener->accept(&peer_address);
    if (!accept_status) {
      log_error("failed to accept client: {}", accept_status.stringify());
      continue;
    }

    const auto peer_ip = IpAddressFormatter::format(peer_address.ip());

    log_info("client {} connected", peer_ip);

    std::thread{[connection_socket = std::move(connection_socket), peer_ip,
                 receive_directory]() mutable {
      ::receiver::Connection connection{std::move(connection_socket), peer_ip, receive_directory};

      while (connection.alive()) {
        connection.update();
      }
    }}.detach();
  }
}