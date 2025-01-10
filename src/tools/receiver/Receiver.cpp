#include "Receiver.hpp"
#include "Connection.hpp"

#include <socklib/Socket.hpp>

#include <tools/Ip.hpp>

#include <net/Port.hpp>

#include <base/Log.hpp>
#include <base/text/Helpers.hpp>

#include <filesystem>
#include <thread>

bool tools::reciever::run(std::span<const std::string_view> args) {
  if (args.size() != 1 && args.size() != 2) {
    log_error("usage: ft receive [target directory] [port]");
    return false;
  }

  uint16_t port = net::default_port;

  if (args.size() == 2) {
    if (!base::text::to_number(args[1], port)) {
      log_error("invalid port `{}`", args[1]);
      return false;
    }
  }

  auto [bind_status, listener] = sock::Listener::bind(
    tools::SocketIpAddress(tools::IpAddress::unspecified(), port), {
                                                                     .reuse_address = true,
                                                                   });
  if (!bind_status) {
    log_error("failed to bind receiver to port {}: {}", port, bind_status.stringify());
    return false;
  }

  log_info("listening at port {}...", port);

  const std::string receive_directory = std::string(args[0]);
  if (!std::filesystem::exists(receive_directory)) {
    std::error_code ec{};
    std::filesystem::create_directories(receive_directory, ec);
    if (ec != std::error_code{}) {
      log_error("failed to create directory `{}`", receive_directory);
      return false;
    }
  } else if (!std::filesystem::is_directory(receive_directory)) {
    log_error("`{}` is not a directory", receive_directory);
    return false;
  }

  log_info("receiving to `{}`", receive_directory);

  while (true) {
    tools::SocketIpAddress peer_address;

    auto [accept_status, connection_socket] = listener.accept(&peer_address);
    if (!accept_status) {
      log_error("failed to accept client: {}", accept_status.stringify());
      continue;
    }

    const auto peer_ip = peer_address.ip().stringify();

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