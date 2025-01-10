#include "Socket.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#define SOCKLIB_WINDOWS
#elif defined(__linux__)
#define SOCKLIB_LINUX
#elif defined(__APPLE__)
#define SOCKLIB_APPLE
#else
#error "Unsupported platform"
#endif

#if defined(SOCKLIB_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct SockaddrBuffer {
  alignas(32) uint8_t data[512];
};

enum class ErrorSource {
  Accept,
  Socket,
  Bind,
  Listen,
  Recv,
  Send,
  Connect,
  FcntlOrIoctlsocket,
  GetSockName,
  GetPeerName,
  Getaddrinfo,
  Setsockopt,
  Poll,
  SocketPair,
};

static int address_type_to_protocol(sock::SocketAddress::Type type) {
  switch (type) {
    case sock::SocketAddress::Type::IpV4:
      return AF_INET;
    case sock::SocketAddress::Type::IpV6:
      return AF_INET6;
    default:
      return AF_MAX;
  }
}

template <typename Fn>
static void socket_address_convert_to_raw(const sock::SocketAddress& address, Fn&& callback) {
  switch (address.type()) {
    case sock::SocketAddress::Type::IpV4: {
      const auto address_ipv4 = static_cast<const sock::SocketIpV4Address&>(address);
      const auto components = address_ipv4.ip().components();

      sockaddr_in sockaddr_in{};
      sockaddr_in.sin_family = AF_INET;
      sockaddr_in.sin_port = htons(address_ipv4.port());
      sockaddr_in.sin_addr.s_addr =
        (uint32_t(components[0]) << 0) | (uint32_t(components[1]) << 8) |
        (uint32_t(components[2]) << 16) | (uint32_t(components[3]) << 24);

      callback(reinterpret_cast<const sockaddr*>(&sockaddr_in), sizeof(sockaddr_in));

      break;
    }

    case sock::SocketAddress::Type::IpV6: {
      const auto address_ipv6 = static_cast<const sock::SocketIpV6Address&>(address);
      const auto components = address_ipv6.ip().components();

      sockaddr_in6 sockaddr_in6{};
      sockaddr_in6.sin6_family = AF_INET6;
      sockaddr_in6.sin6_port = htons(address_ipv6.port());

      for (size_t i = 0; i < 8; ++i) {
#if defined(SOCKLIB_WINDOWS)
        sockaddr_in6.sin6_addr.u.Word[i] = htons(components[i]);
#elif defined(SOCKLIB_APPLE)
        sockaddr_in6.sin6_addr.__u6_addr.__u6_addr16[i] = htons(components[i]);
#else
        reinterpret_cast<uint16_t*>(sockaddr_in6.sin6_addr.s6_addr)[i] = htons(components[i]);
#endif
      }

      callback(reinterpret_cast<const sockaddr*>(&sockaddr_in6), sizeof(sockaddr_in6));

      break;
    }

    default:
      break;
  }
}

static bool socket_address_convert_from_raw(const sockaddr* sockaddr_buffer,
                                            sock::SocketAddress& address) {
  switch (address.type()) {
    case sock::SocketAddress::Type::IpV4: {
      if (sockaddr_buffer->sa_family != AF_INET) {
        return false;
      }

      const auto dest = reinterpret_cast<sock::SocketIpV4Address*>(&address);
      const auto source = reinterpret_cast<const sockaddr_in*>(sockaddr_buffer);

      const auto ip = source->sin_addr.s_addr;
      const std::array<uint8_t, 4> components{uint8_t(ip >> 0), uint8_t(ip >> 8), uint8_t(ip >> 16),
                                              uint8_t(ip >> 24)};

      *dest = sock::SocketIpV4Address(sock::IpV4Address(components), ntohs(source->sin_port));

      return true;
    }

    case sock::SocketAddress::Type::IpV6: {
      if (sockaddr_buffer->sa_family != AF_INET6) {
        return false;
      }

      const auto dest = reinterpret_cast<sock::SocketIpV6Address*>(&address);
      const auto source = reinterpret_cast<const sockaddr_in6*>(sockaddr_buffer);

      std::array<uint16_t, 8> components{};
      for (size_t i = 0; i < 8; ++i) {
#if defined(SOCKLIB_WINDOWS)
        components[i] = ntohs(source->sin6_addr.u.Word[i]);
#elif defined(SOCKLIB_APPLE)
        components[i] = ntohs(source->sin6_addr.__u6_addr.__u6_addr16[i]);
#else
        components[i] = ntohs(reinterpret_cast<const uint16_t*>(source->sin6_addr.s6_addr)[i]);
#endif
      }

      *dest = sock::SocketIpV6Address(sock::IpV6Address(components), ntohs(source->sin6_port));

      return true;
    }
  }

  return false;
}

static bool socket_address_convert_from_raw(const SockaddrBuffer& buffer,
                                            sock::SocketAddress& address) {
  auto const sockaddr_buffer = reinterpret_cast<const sockaddr*>(buffer.data);
  return socket_address_convert_from_raw(sockaddr_buffer, address);
}

#if defined(SOCKLIB_WINDOWS)
static constexpr int default_error_value = SOCKET_ERROR;
#else
static constexpr int default_error_value = -1;
#endif

static bool is_error(int value) {
  return value == default_error_value;
}
static bool is_error_ext(intptr_t value) {
  return value == default_error_value;
}

static bool is_valid_socket(sock::detail::RawSocket socket) {
  return socket != sock::detail::invalid_raw_socket;
}

static void close_socket_if_valid(sock::detail::RawSocket socket) {
  if (is_valid_socket(socket)) {
#if defined(SOCKLIB_WINDOWS)
    ::shutdown(socket, SD_BOTH);
    ::closesocket(socket);
#else
    ::shutdown(socket, SHUT_RDWR);
    ::close(socket);
#endif
  }
}

static sock::SystemError last_error_to_system_error(ErrorSource error_source) {
  using EC = sock::SystemError;

#if defined(SOCKLIB_WINDOWS)
  const auto error_num = WSAGetLastError();

  switch (error_num) {
    case WSAEISCONN:
      return EC::AlreadyConnected;
    case WSANOTINITIALISED:
      return EC::NotInitialized;
    case WSAENETDOWN:
      return EC::NetworkSubsystemFailed;
    case WSAEACCES:
      return EC::AccessDenied;
    case WSAEADDRINUSE:
      return EC::AddressInUse;
    case WSAECONNREFUSED:
      return EC::ConnectionRefused;
    case ENETUNREACH:
      return EC::NetworkUnreachable;
    case WSAETIMEDOUT:
      return EC::TimedOut;
    case WSAEWOULDBLOCK:
      return EC::WouldBlock;
    case WSAEALREADY:
      return EC::AlreadyInProgress;
    case WSAEINPROGRESS:
      return EC::NowInProgress;
    case WSAEHOSTUNREACH:
      return EC::HostUnreachable;
    case WSAEBADF:
    case WSAENOTSOCK:
      return EC::InvalidSocket;
    case WSAECONNRESET:
      return EC::ConnectionReset;
    case WSAEDESTADDRREQ:
      return EC::NoPeerAddress;
    case WSAESHUTDOWN:
      return EC::SocketShutdown;
    case WSAEADDRNOTAVAIL:
      return EC::AddressNotAvailable;
    case WSAEINVAL:
      return EC::InvalidValue;
    default:
      return EC::Unknown;
  }
#else
  const auto error_num = errno;

  switch (error_num) {
    case EISCONN:
      return EC::AlreadyConnected;
    case ENETDOWN:
      return EC::NetworkSubsystemFailed;
    case EACCES:
    case EPERM:
      return EC::AccessDenied;
    case EADDRINUSE:
      return EC::AddressInUse;
    case ECONNREFUSED:
      return EC::ConnectionRefused;
    case ENETUNREACH:
      return EC::NetworkUnreachable;
    case ETIMEDOUT:
      return EC::TimedOut;
    case EWOULDBLOCK:
      return EC::WouldBlock;
    case EALREADY:
      return EC::AlreadyInProgress;
    case EINPROGRESS:
      return EC::NowInProgress;
    case EHOSTUNREACH:
      return EC::HostUnreachable;
    case EBADF:
    case ENOTSOCK:
      return EC::InvalidSocket;
    case ECONNRESET:
      return EC::ConnectionReset;
    case EDESTADDRREQ:
      return EC::NoPeerAddress;
    case EPIPE:
      return EC::SocketShutdown;
    case EADDRNOTAVAIL:
      return EC::AddressNotAvailable;
    case EINVAL:
      return EC::InvalidValue;
    default:
      return EC::Unknown;
  }
#endif
}

static sock::Status last_error_to_status(sock::Error error, ErrorSource error_source) {
  return sock::Status{error, sock::Error::None, last_error_to_system_error(error_source)};
}

static sock::Status wrap_status(const sock::Status& status, sock::Error error) {
  return sock::Status{error, status.error, status.system_error};
}

template <typename T>
static sock::Status set_socket_option(sock::detail::RawSocket socket,
                                      int level,
                                      int option,
                                      const T& value) {
  if (is_error(::setsockopt(socket, level, option, reinterpret_cast<const char*>(&value),
                            sizeof(value)))) {
    return last_error_to_status(sock::Error::SetSocketOptionFailed, ErrorSource::Setsockopt);
  }
  return {};
}

template <typename Fn>
static auto handle_eintr(Fn&& callback) {
  while (true) {
    const auto result = callback();
#if defined(SOCKLIB_WINDOWS)
    if (is_error(result) && WSAGetLastError() == WSAEINTR) {
      continue;
    }
#else
    if (is_error(result) && errno == EINTR) {
      continue;
    }
#endif
    return result;
  }
}

static sock::Status set_socket_option_timeout_ms(sock::detail::RawSocket socket,
                                                 int level,
                                                 int option,
                                                 uint64_t timeout_ms) {
#if defined(SOCKLIB_WINDOWS)
  if (timeout_ms > std::numeric_limits<int>::max()) {
    return sock::Status{sock::Error::SetSocketOptionFailed, sock::Error::TimeoutTooLarge};
  }
  return set_socket_option<int>(socket, level, option, timeout_ms);
#else
  timeval timeout_timeval{};
  timeout_timeval.tv_sec = static_cast<long>(timeout_ms / 1000);
  timeout_timeval.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
  return set_socket_option<timeval>(socket, level, option, timeout_timeval);
#endif
}

static sock::Status set_socket_non_blocking(sock::detail::RawSocket socket, bool non_blocking) {
#if defined(SOCKLIB_WINDOWS)
  u_long non_blocking_value = static_cast<u_long>(non_blocking);
  if (is_error(::ioctlsocket(socket, FIONBIO, &non_blocking_value))) {
    return last_error_to_status(sock::Error::SetSocketBlockingFailed,
                                ErrorSource::FcntlOrIoctlsocket);
  }
  return {};
#else
  if (is_error(
        ::fcntl(socket, F_SETFL,
                (non_blocking ? O_NONBLOCK : 0) | (::fcntl(socket, F_GETFL) & ~O_NONBLOCK)))) {
    return last_error_to_status(sock::Error::SetSocketBlockingFailed,
                                ErrorSource::FcntlOrIoctlsocket);
  }
  return {};
#endif
}

static sock::Status setup_socket(sock::detail::RawSocket socket,
                                 bool reuse_address,
                                 bool reuse_port,
                                 bool non_blocking) {
  sock::Status status{};

#if !defined(SOCKLIB_WINDOWS)
  if (reuse_address) {
    status = set_socket_option<int>(socket, SOL_SOCKET, SO_REUSEADDR, 1);
    if (!status) {
      return wrap_status(status, sock::Error::SocketSetupFailed);
    }
  }

  if (reuse_port) {
    status = set_socket_option<int>(socket, SOL_SOCKET, SO_REUSEPORT, 1);
    if (!status) {
      return wrap_status(status, sock::Error::SocketSetupFailed);
    }
  }
#else
  if (reuse_port) {
    status = set_socket_option<int>(socket, SOL_SOCKET, SO_REUSEADDR, 1);
    if (!status) {
      return wrap_status(status, sock::Error::SocketSetupFailed);
    }
  }
#endif

  // Can fail.
  (void)set_socket_option<int>(socket, IPPROTO_IPV6, IPV6_V6ONLY, 0);

  if (non_blocking) {
    status = set_socket_non_blocking(socket, true);
    if (!status) {
      return wrap_status(status, sock::Error::SocketSetupFailed);
    }
  }

  return {};
}

template <typename TResult, typename TAddress>
static TResult resolve_ip_generic(int family, std::string_view hostname) {
  addrinfo* resolved{};
  addrinfo hints{};

  hints.ai_family = family;
  hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;

  const auto hostname_s = std::string(hostname);

  // Different return value semantics, check if != 0.
  const auto addrinfo_result = ::getaddrinfo(hostname_s.c_str(), nullptr, &hints, &resolved);
  if (addrinfo_result != 0) {
    return {
      .status = {sock::Error::HostnameNotFound},
    };
  }

  for (auto current = resolved; current; current = current->ai_next) {
    if (current->ai_family != hints.ai_family) {
      continue;
    }

    if constexpr (std::is_same_v<TAddress, sock::IpV4Address>) {
      reinterpret_cast<sockaddr_in*>(current->ai_addr)->sin_port = 0;
    } else if constexpr (std::is_same_v<TAddress, sock::IpV6Address>) {
      reinterpret_cast<sockaddr_in6*>(current->ai_addr)->sin6_port = 0;
    }

    TAddress address;
    if (socket_address_convert_from_raw(current->ai_addr, address)) {
      ::freeaddrinfo(resolved);
      return {
        .value = address.ip(),
      };
    }
  }

  ::freeaddrinfo(resolved);
  return {
    .status = {sock::Error::HostnameNotFound},
  };
}

sock::Result<sock::IpV4Address> sock::IpResolver::resolve_ipv4(std::string_view hostname) {
  return resolve_ip_generic<Result<IpV4Address>, SocketIpV4Address>(AF_INET, hostname);
}

sock::Result<sock::IpV6Address> sock::IpResolver::resolve_ipv6(std::string_view hostname) {
  return resolve_ip_generic<Result<IpV6Address>, SocketIpV6Address>(AF_INET6, hostname);
}

template <typename Result, typename Fn>
static Result resolve_and_run(sock::IpVersion ip_version,
                              std::string_view hostname,
                              uint16_t port,
                              Fn&& callback) {
  switch (ip_version) {
    case sock::IpVersion::V4: {
      const auto resolved = sock::IpResolver::resolve_ipv4(hostname);
      if (!resolved) {
        return {
          .status = wrap_status(resolved.status, sock::Error::IpResolveFailed),
        };
      }
      return callback(sock::SocketIpV4Address(resolved.value, port));
    }
    case sock::IpVersion::V6: {
      const auto resolved = sock::IpResolver::resolve_ipv6(hostname);
      if (!resolved) {
        return {
          .status = wrap_status(resolved.status, sock::Error::IpResolveFailed),
        };
      }
      return callback(sock::SocketIpV6Address(resolved.value, port));
    }
    default: {
      return {
        .status = {sock::Error::IpResolveFailed, sock::Error::InvalidAddressType},
      };
    }
  }
}

struct sock::detail::RawSocketAccessor {
  static RawSocket get(const Socket& socket) { return socket.raw_socket_; }
};

sock::Socket::Socket(detail::RawSocket raw_socket) : raw_socket_(raw_socket) {}

sock::Socket::Socket(Socket&& other) noexcept {
  raw_socket_ = other.raw_socket_;
  other.raw_socket_ = detail::invalid_raw_socket;
}

sock::Socket& sock::Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    close_socket_if_valid(raw_socket_);

    raw_socket_ = other.raw_socket_;
    other.raw_socket_ = detail::invalid_raw_socket;
  }
  return *this;
}

sock::Socket::~Socket() {
  close_socket_if_valid(raw_socket_);
}

bool sock::Socket::valid() const {
  return is_valid_socket(raw_socket_);
}

sock::Status sock::Socket::set_non_blocking(bool non_blocking) {
  return set_socket_non_blocking(raw_socket_, non_blocking);
}

sock::Status sock::Socket::local_address(SocketAddress& address) const {
  SockaddrBuffer socket_address;
  socklen_t socket_address_length = sizeof(socket_address);

  if (is_error(::getsockname(raw_socket_, reinterpret_cast<sockaddr*>(socket_address.data),
                             &socket_address_length))) {
    return last_error_to_status(Error::GetLocalAddressFailed, ErrorSource::GetSockName);
  }

  if (!socket_address_convert_from_raw(socket_address, address)) {
    return Status{Error::GetLocalAddressFailed, Error::AddressConversionFailed};
  }

  return {};
}

sock::Status sock::Socket::peer_address(SocketAddress& address) const {
  SockaddrBuffer socket_address;
  socklen_t socket_address_length = sizeof(socket_address);

  if (is_error(::getpeername(raw_socket_, reinterpret_cast<sockaddr*>(socket_address.data),
                             &socket_address_length))) {
    return last_error_to_status(Error::GetPeerAddressFailed, ErrorSource::GetPeerName);
  }

  if (!socket_address_convert_from_raw(socket_address, address)) {
    return Status{Error::GetPeerAddressFailed, Error::AddressConversionFailed};
  }

  return {};
}

sock::Status sock::detail::RwSocket::set_receive_timeout_ms(uint64_t timeout_ms) {
  return set_socket_option_timeout_ms(raw_socket_, SOL_SOCKET, SO_RCVTIMEO, timeout_ms);
}

sock::Status sock::detail::RwSocket::set_send_timeout_ms(uint64_t timeout_ms) {
  return set_socket_option_timeout_ms(raw_socket_, SOL_SOCKET, SO_SNDTIMEO, timeout_ms);
}

sock::Status sock::detail::RwSocket::set_broadcast_enabled(bool broadcast_enabled) {
  return set_socket_option<int>(raw_socket_, SOL_SOCKET, SO_BROADCAST, broadcast_enabled ? 1 : 0);
}

sock::Result<sock::DatagramSocket> sock::DatagramSocket::bind(
  const SocketAddress& address,
  const BindParameters& bind_parameters) {
  const auto datagram_socket = ::socket(address_type_to_protocol(address.type()), SOCK_DGRAM, 0);
  if (!is_valid_socket(datagram_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed, ErrorSource::Socket),
    };
  }

  const auto setup_status = setup_socket(datagram_socket, bind_parameters.reuse_address,
                                         bind_parameters.reuse_port, bind_parameters.non_blocking);
  if (!setup_status) {
    close_socket_if_valid(datagram_socket);
    return {
      .status = setup_status,
    };
  }

  int bind_result{default_error_value};
  socket_address_convert_to_raw(
    address, [&](const sockaddr* socket_address, socklen_t sockaddr_size) {
      bind_result = ::bind(datagram_socket, socket_address, sockaddr_size);
    });
  if (is_error(bind_result)) {
    const auto status = last_error_to_status(Error::BindFailed, ErrorSource::Bind);
    close_socket_if_valid(datagram_socket);
    return {
      .status = status,
    };
  }

  return {
    .status = {},
    .value = DatagramSocket(datagram_socket),
  };
}

sock::Result<sock::DatagramSocket> sock::DatagramSocket::bind(
  IpVersion ip_version,
  std::string_view hostname,
  uint16_t port,
  const BindParameters& bind_parameters) {
  return resolve_and_run<Result<DatagramSocket>>(
    ip_version, hostname, port,
    [&](const SocketAddress& resolved_address) { return bind(resolved_address, bind_parameters); });
}

sock::Result<sock::DatagramSocket> sock::DatagramSocket::create(
  SocketAddress::Type type,
  const CreateParameters& create_parameters) {
  const auto datagram_socket = ::socket(address_type_to_protocol(type), SOCK_DGRAM, 0);
  if (!is_valid_socket(datagram_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed, ErrorSource::Socket),
    };
  }

  const auto setup_status =
    setup_socket(datagram_socket, false, false, create_parameters.non_blocking);
  if (!setup_status) {
    close_socket_if_valid(datagram_socket);
    return {
      .status = setup_status,
    };
  }

  return {
    .status = {},
    .value = DatagramSocket{datagram_socket},
  };
}

sock::Result<size_t> sock::DatagramSocket::send(const SocketAddress& to,
                                                const void* data,
                                                size_t data_size) {
  if (data_size == 0) {
    return {};
  }

  if (data_size > std::numeric_limits<int>::max()) {
    return {
      .status = {Error::SendFailed, Error::SizeTooLarge},
    };
  }

  intptr_t result{default_error_value};
  socket_address_convert_to_raw(to, [&](const sockaddr* socket_address, socklen_t sockaddr_size) {
    result = handle_eintr([&] {
      return ::sendto(raw_socket_, reinterpret_cast<const char*>(data), data_size, MSG_NOSIGNAL,
                      socket_address, sockaddr_size);
    });
  });

  if (result == 0) {
    return {
      .status = Status{Error::SendFailed, Error::None, SystemError::Disconnected},
    };
  }
  if (is_error_ext(result)) {
    return {
      .status = last_error_to_status(Error::SendFailed, ErrorSource::Send),
    };
  }

  return {
    .status = {},
    .value = size_t(result),
  };
}

sock::Result<size_t> sock::DatagramSocket::send_all(const SocketAddress& to,
                                                    const void* data,
                                                    size_t data_size) {
  auto current = reinterpret_cast<const uint8_t*>(data);
  size_t bytes_sent = 0;

  while (bytes_sent < data_size) {
    const auto [status, current_sent] = send(to, current, data_size - bytes_sent);
    if (!status) {
      return {
        .status = status,
        .value = bytes_sent,
      };
    }
    current += current_sent;
    bytes_sent += current_sent;
  }

  return {
    .status = {},
    .value = bytes_sent,
  };
}

sock::Result<size_t> sock::DatagramSocket::receive(SocketAddress& from,
                                                   void* data,
                                                   size_t data_size) {
  if (data_size == 0) {
    return {};
  }

  if (data_size > std::numeric_limits<int>::max()) {
    return {
      .status = {Error::ReceiveFailed, Error::SizeTooLarge},
    };
  }

  SockaddrBuffer socket_address;
  socklen_t socket_address_length = sizeof(socket_address);

  const auto result = handle_eintr([&] {
    return ::recvfrom(raw_socket_, reinterpret_cast<char*>(data), data_size, 0,
                      reinterpret_cast<sockaddr*>(socket_address.data), &socket_address_length);
  });
  if (result == 0) {
    return {
      .status = Status{Error::ReceiveFailed, Error::None, SystemError::Disconnected},
    };
  }
  if (is_error_ext(result)) {
    return {
      .status = last_error_to_status(Error::ReceiveFailed, ErrorSource::Recv),
    };
  }

  if (!socket_address_convert_from_raw(socket_address, from)) {
    return {
      .status = {Error::ReceiveFailed, Error::AddressConversionFailed},
    };
  }

  return {
    .status = {},
    .value = size_t(result),
  };
}

sock::Result<sock::StreamSocket> sock::StreamSocket::connect(
  const SocketAddress& address,
  const ConnectParameters& connect_parameters) {
  const auto connection_socket = ::socket(address_type_to_protocol(address.type()), SOCK_STREAM, 0);
  if (!is_valid_socket(connection_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed, ErrorSource::Socket),
    };
  }

  const auto setup_status = setup_socket(connection_socket, false, false, false);
  if (!setup_status) {
    close_socket_if_valid(connection_socket);
    return {
      .status = setup_status,
    };
  }

  int connect_status{default_error_value};
  socket_address_convert_to_raw(address, [&](const sockaddr* sockaddr, socklen_t sockaddr_size) {
    connect_status =
      handle_eintr([&] { return ::connect(connection_socket, sockaddr, sockaddr_size); });
  });
  if (is_error(connect_status)) {
    const auto status = last_error_to_status(Error::ConnectFailed, ErrorSource::Connect);
    close_socket_if_valid(connect_status);
    return {
      .status = status,
    };
  }

  return {
    .status = {},
    .value = StreamSocket{connection_socket},
  };
}

sock::Result<sock::StreamSocket> sock::StreamSocket::connect(
  IpVersion ip_version,
  std::string_view hostname,
  uint16_t port,
  const ConnectParameters& connect_parameters) {
  return resolve_and_run<Result<StreamSocket>>(
    ip_version, hostname, port, [&](const SocketAddress& resolved_address) {
      return connect(resolved_address, connect_parameters);
    });
}

#ifdef SOCKLIB_WINDOWS
template <typename Address>
static sock::Result<std::pair<sock::StreamSocket, sock::StreamSocket>> windows_socket_pair_emulated(
  bool non_blocking) {
  auto [listener_status, listener] =
    sock::Listener::bind(Address{Address::Ip::loopback(), 0}, {
                                                                .non_blocking = true,
                                                                .max_pending_connections = 1,
                                                              });
  if (!listener_status) {
    return {.status = listener_status};
  }

  auto [local_address_status, local_address] = listener.local_address<Address>();
  if (!local_address_status) {
    return {.status = local_address_status};
  }

  auto [connect_status, connection] = sock::ConnectingSocket::initiate_connection(
    Address{Address::Ip::loopback(), local_address.port()});
  if (!connect_status) {
    return {.status = connect_status};
  }

  auto [accept_status, accepted] = listener.accept();
  if (!accept_status) {
    return {.status = accept_status};
  }

  sock::StreamSocket socket_1 = std::move(accepted);
  sock::StreamSocket socket_2 = std::move(connection.connected);
  if (!socket_2) {
    auto [connect_2_status, connection_2] = connection.connecting.connect();
    if (!connect_2_status) {
      return {.status = connect_2_status};
    }
    socket_2 = std::move(connection_2);
  }

  if (auto status = socket_1.set_non_blocking(non_blocking); !status) {
    return {.status = status};
  }
  if (auto status = socket_2.set_non_blocking(non_blocking); !status) {
    return {.status = status};
  }

  return {.status = {}, .value = {std::move(socket_1), std::move(socket_2)}};
}
#endif

sock::Result<std::pair<sock::StreamSocket, sock::StreamSocket>> sock::StreamSocket::connected_pair(
  const ConnectedPairParameters& pair_parameters) {
#ifdef SOCKLIB_WINDOWS
  sock::Status last_status = {};

  for (int i = 0; i < 4; ++i) {
    {
      auto [status, pair] =
        windows_socket_pair_emulated<sock::SocketIpV6Address>(pair_parameters.non_blocking);
      if (status) {
        return {.status = status, .value = std::move(pair)};
      } else {
        last_status = status;
      }
    }
    {
      auto [status, pair] =
        windows_socket_pair_emulated<sock::SocketIpV4Address>(pair_parameters.non_blocking);
      if (status) {
        return {.status = status, .value = std::move(pair)};
      } else {
        last_status = status;
      }
    }
  }

  return {.status = last_status};
#else
  detail::RawSocket sockets[2]{};
  if (is_error(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets))) {
    return {
      .status = last_error_to_status(Error::SocketPairFailed, ErrorSource::SocketPair),
    };
  }
  return {.status = {}, .value = {StreamSocket{sockets[0]}, StreamSocket{sockets[1]}}};
#endif
}

sock::Result<size_t> sock::StreamSocket::send(const void* data, size_t data_size) {
  if (data_size == 0) {
    return {};
  }

  if (data_size > std::numeric_limits<int>::max()) {
    return {
      .status = {Error::SendFailed, Error::SizeTooLarge},
    };
  }

  const auto result = handle_eintr([&] {
    return ::send(raw_socket_, reinterpret_cast<const char*>(data), data_size, MSG_NOSIGNAL);
  });
  if (result == 0) {
    return {
      .status = Status{Error::SendFailed, Error::None, SystemError::Disconnected},
    };
  }
  if (is_error_ext(result)) {
    return {
      .status = last_error_to_status(Error::SendFailed, ErrorSource::Send),
    };
  }

  return {
    .status = {},
    .value = size_t(result),
  };
}

sock::Result<size_t> sock::StreamSocket::send_all(const void* data, size_t data_size) {
  auto current = reinterpret_cast<const uint8_t*>(data);
  size_t bytes_sent = 0;

  while (bytes_sent < data_size) {
    const auto [status, current_sent] = send(current, data_size - bytes_sent);
    if (!status) {
      return {
        .status = status,
        .value = bytes_sent,
      };
    }
    current += current_sent;
    bytes_sent += current_sent;
  }

  return {
    .status = {},
    .value = bytes_sent,
  };
}

sock::Result<size_t> sock::StreamSocket::receive(void* data, size_t data_size) {
  if (data_size == 0) {
    return {};
  }

  if (data_size > std::numeric_limits<int>::max()) {
    return {
      .status = {Error::ReceiveFailed, Error::SizeTooLarge},
    };
  }

  const auto result =
    handle_eintr([&] { return ::recv(raw_socket_, reinterpret_cast<char*>(data), data_size, 0); });
  if (result == 0) {
    return {
      .status = Status{Error::ReceiveFailed, Error::None, SystemError::Disconnected},
    };
  }
  if (is_error_ext(result)) {
    return {
      .status = last_error_to_status(Error::ReceiveFailed, ErrorSource::Recv),
    };
  }

  return {
    .status = {},
    .value = size_t(result),
  };
}

sock::ConnectingSocket::ConnectingSocket(detail::RawSocket raw_socket,
                                         std::unique_ptr<uint64_t[]> socket_address,
                                         size_t sockt_address_size)
    : Socket(raw_socket),
      socket_address(std::move(socket_address)),
      sockt_address_size(sockt_address_size) {}

sock::ConnectingSocket::ConnectingSocket(ConnectingSocket&& other) noexcept {
  raw_socket_ = other.raw_socket_;
  socket_address = std::move(other.socket_address);
  sockt_address_size = other.sockt_address_size;

  other.raw_socket_ = detail::invalid_raw_socket;
  other.socket_address = {};
  other.sockt_address_size = 0;
}

sock::Result<sock::ConnectingSocket::SocketPair> sock::ConnectingSocket::initiate_connection(
  const SocketAddress& address,
  const ConnectParameters& connect_parameters) {
  const auto connection_socket = ::socket(address_type_to_protocol(address.type()), SOCK_STREAM, 0);
  if (!is_valid_socket(connection_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed, ErrorSource::Socket),
    };
  }

  const auto setup_status = setup_socket(connection_socket, false, false, true);
  if (!setup_status) {
    close_socket_if_valid(connection_socket);
    return {
      .status = setup_status,
    };
  }

  std::unique_ptr<uint64_t[]> socket_address_buffer;
  size_t socket_address_size{};

  socket_address_convert_to_raw(address, [&](const sockaddr* sockaddr, socklen_t sockaddr_size) {
    socket_address_buffer =
      std::make_unique<uint64_t[]>((sockaddr_size + sizeof(uint64_t) - 1) / sizeof(uint64_t));
    std::memcpy(socket_address_buffer.get(), sockaddr, sockaddr_size);
    socket_address_size = size_t(sockaddr_size);
  });

  const int connect_status = handle_eintr([&] {
    return ::connect(connection_socket, reinterpret_cast<sockaddr*>(socket_address_buffer.get()),
                     socket_address_size);
  });

  if (is_error(connect_status)) {
    const auto status = last_error_to_status(Error::ConnectFailed, ErrorSource::Connect);
    if (!status.would_block() && !status.has_error(SystemError::AlreadyInProgress) &&
        !status.has_error(SystemError::NowInProgress)) {
      close_socket_if_valid(connect_status);
      return {
        .status = status,
      };
    }

    return {
      .status = {},
      .value = {ConnectingSocket{connection_socket, std::move(socket_address_buffer),
                                 socket_address_size},
                StreamSocket{}},
    };
  } else {
    return {
      .status = {},
      .value = {ConnectingSocket{}, StreamSocket{connection_socket}},
    };
  }
}

sock::Result<sock::StreamSocket> sock::ConnectingSocket::connect() {
  if (raw_socket_ == detail::invalid_raw_socket) {
    return {
      .status = Status{Error::ConnectFailed, Error::None, SystemError::None},
    };
  }

  const int connect_status = handle_eintr([&] {
    return ::connect(raw_socket_, reinterpret_cast<sockaddr*>(socket_address.get()),
                     sockt_address_size);
  });

  if (is_error(connect_status)) {
    auto status = last_error_to_status(Error::ConnectFailed, ErrorSource::Connect);
    if (status.system_error != SystemError::AlreadyConnected) {
      auto is_expected_error = status.would_block() ||
                               status.has_error(SystemError::AlreadyInProgress) ||
                               status.has_error(SystemError::NowInProgress);
#ifdef SOCKLIB_WINDOWS
      // In order to preserve backward compatibility, this error is reported as WSAEINVAL to
      // Windows Sockets 1.1.
      is_expected_error |= status.system_error == SystemError::InvalidValue;
#endif

      if (is_expected_error) {
        status.system_error = SystemError::WouldBlock;
      }

      return {
        .status = status,
      };
    }
  }

  const auto socket = raw_socket_;

  raw_socket_ = detail::invalid_raw_socket;
  socket_address = {};
  sockt_address_size = 0;

  return {.status = {}, .value = StreamSocket{socket}};
}

sock::Result<sock::Listener> sock::Listener::bind(const SocketAddress& address,
                                                  const BindParameters& bind_parameters) {
  const auto listener_socket = ::socket(address_type_to_protocol(address.type()), SOCK_STREAM, 0);
  if (!is_valid_socket(listener_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed, ErrorSource::Socket),
    };
  }

  const auto setup_status = setup_socket(listener_socket, bind_parameters.reuse_address,
                                         bind_parameters.reuse_port, bind_parameters.non_blocking);
  if (!setup_status) {
    close_socket_if_valid(listener_socket);
    return {
      .status = setup_status,
    };
  }

  int bind_result{default_error_value};
  socket_address_convert_to_raw(
    address, [&](const sockaddr* socket_address, socklen_t sockaddr_size) {
      bind_result = ::bind(listener_socket, socket_address, sockaddr_size);
    });
  if (is_error(bind_result)) {
    const auto status = last_error_to_status(Error::BindFailed, ErrorSource::Bind);
    close_socket_if_valid(listener_socket);
    return {
      .status = status,
    };
  }

  const auto backlog_size = std::min(bind_parameters.max_pending_connections, SOMAXCONN);
  if (is_error(::listen(listener_socket, backlog_size))) {
    const auto status = last_error_to_status(Error::ListenFailed, ErrorSource::Listen);
    close_socket_if_valid(listener_socket);
    return {
      .status = status,
    };
  }

  return {
    .status = {},
    .value = Listener{listener_socket},
  };
}

sock::Result<sock::Listener> sock::Listener::bind(IpVersion ip_version,
                                                  std::string_view hostname,
                                                  uint16_t port,
                                                  const BindParameters& bind_parameters) {
  return resolve_and_run<Result<Listener>>(
    ip_version, hostname, port,
    [&](const SocketAddress& resolved_address) { return bind(resolved_address, bind_parameters); });
}

sock::Result<sock::StreamSocket> sock::Listener::accept(SocketAddress* peer_address) {
  SockaddrBuffer socket_address;
  socklen_t socket_address_length = sizeof(socket_address);

  const detail::RawSocket accepted_socket = handle_eintr([&]() -> detail::RawSocket {
    if (peer_address) {
      return ::accept(raw_socket_, reinterpret_cast<sockaddr*>(socket_address.data),
                      &socket_address_length);
    } else {
      return ::accept(raw_socket_, nullptr, nullptr);
    }
  });

  if (!is_valid_socket(accepted_socket)) {
    return {
      .status = last_error_to_status(Error::AcceptFailed, ErrorSource::Accept),
    };
  }

  if (peer_address) {
    if (!socket_address_convert_from_raw(socket_address, *peer_address)) {
      close_socket_if_valid(accepted_socket);
      return {
        .status = {Error::AcceptFailed, Error::AddressConversionFailed},
      };
    }
  }

  return {
    .status = {},
    .value = StreamSocket{accepted_socket},
  };
}

#ifdef SOCKLIB_WINDOWS

class PollCanceller {
  sock::StreamSocket write_socket;
  sock::StreamSocket read_socket;
  bool initialized{false};

 public:
  PollCanceller() {
    auto [status, pair] = sock::StreamSocket::connected_pair({
      .non_blocking = true,
    });
    if (status) {
      write_socket = std::move(pair.first);
      read_socket = std::move(pair.second);
      initialized = true;
    }
  }

  bool valid() const { return initialized; }

  sock::detail::RawSocket cancel_socket() const {
    return sock::detail::RawSocketAccessor::get(read_socket);
  }

  bool drain() {
    uint8_t buffer[32];

    bool first = true;

    while (true) {
      const auto [receive_status, received_bytes] = read_socket.receive(buffer);
      if (first) {
        if (!receive_status) {
          return false;
        }
        first = false;
      }

      if (receive_status.would_block() || received_bytes < sizeof(buffer)) {
        return true;
      }

      if (!receive_status) {
        return false;
      }
    }
  }

  bool signal() {
    uint8_t buffer[1]{};
    const auto [send_status, sent_bytes] = write_socket.send(buffer);
    return send_status && sent_bytes == 1;
  }
};

#else

class PollCanceller {
  int read_pipe{-1};
  int write_pipe{-1};
  bool initialized{false};

 public:
  PollCanceller() {
    int pipe_fds[2]{};
    if (::pipe(pipe_fds) == 0) {
      read_pipe = pipe_fds[0];
      write_pipe = pipe_fds[1];

      if (::fcntl(read_pipe, F_SETFL, O_NONBLOCK | (::fcntl(read_pipe, F_GETFL))) == -1) {
        return;
      }

      if (::fcntl(write_pipe, F_SETFL, O_NONBLOCK | (::fcntl(write_pipe, F_GETFL))) == -1) {
        return;
      }

      initialized = true;
    }
  }

  ~PollCanceller() {
    if (read_pipe != -1) {
      ::close(read_pipe);
    }
    if (write_pipe != -1) {
      ::close(write_pipe);
    }
  }

  bool valid() const { return initialized; }

  sock::detail::RawSocket cancel_socket() const { return read_pipe; }

  bool drain() {
    uint8_t buffer[32];

    bool first = true;

    while (true) {
      const auto result = handle_eintr([&] { return ::read(read_pipe, buffer, sizeof(buffer)); });
      if (first) {
        if (result <= 0) {
          return false;
        }
        first = false;
      }

      if ((result == -1 && errno == EWOULDBLOCK) || result < sizeof(buffer)) {
        return true;
      }

      if (result <= 0) {
        return false;
      }
    }
  }

  bool signal() {
    uint8_t buffer[1]{};
    const auto result =
      handle_eintr([this, buffer] { return ::write(write_pipe, buffer, sizeof(buffer)); });
    return result == 1;
  }
};

#endif

class PollerImpl : public sock::Poller {
#if defined(SOCKLIB_WINDOWS)
  using RawEntry = WSAPOLLFD;
#else
  using RawEntry = pollfd;
#endif

  std::vector<RawEntry> raw_entries;

  PollCanceller canceller;
  std::atomic_bool cancel_pending{false};

 public:
  operator bool() const { return canceller.valid(); }

  sock::Result<size_t> poll(std::span<PollEntry> entries, int timeout_ms) override {
    raw_entries.resize(entries.size());

    for (size_t i = 0; i < entries.size(); i++) {
      auto& source = entries[i];
      auto& dest = raw_entries[i];

      source.status_events = {};

      dest.fd = source.socket ? sock::detail::RawSocketAccessor::get(*source.socket)
                              : sock::detail::RawSocket(-1);
      dest.events = {};

      if ((source.query_events & QueryEvents::CanReceiveFrom) == QueryEvents::CanReceiveFrom) {
        dest.events |= POLLIN;
      }
      if ((source.query_events & QueryEvents::CanSendTo) == QueryEvents::CanSendTo) {
        dest.events |= POLLOUT;
      }
    }

    if (cancel_pending) {
      if (canceller.drain()) {
        cancel_pending = false;
        return {
          .status = {},
          .value = {},
        };
      }
    }

    raw_entries.push_back({
      .fd = canceller.cancel_socket(),
      .events = POLLIN,
    });

#if defined(SOCKLIB_WINDOWS)
    const auto poll_result = handle_eintr(
      [&] { return ::WSAPoll(raw_entries.data(), ULONG(raw_entries.size()), timeout_ms); });
#else
    const auto poll_result =
      handle_eintr([&] { return ::poll(raw_entries.data(), int(raw_entries.size()), timeout_ms); });
#endif

    if (is_error(poll_result)) {
      return {
        .status = last_error_to_status(sock::Error::PollFailed, ErrorSource::Poll),
      };
    }

    auto signaled_entries = size_t(poll_result);

    if (const auto& last = raw_entries[entries.size()]; last.revents) {
      const bool signaled_input = last.revents & POLLIN;
      bool cancellation_error = last.revents & (POLLERR | POLLHUP | POLLNVAL);

      if (signaled_input) {
        if (canceller.drain()) {
          cancel_pending.store(false);
        } else {
          cancellation_error = true;
        }
      }

      if (cancellation_error) {
        return {
          .status = sock::Status{sock::Error::PollFailed, sock::Error::CancellationFailed},
        };
      }

      signaled_entries -= 1;
    }

    if (signaled_entries > 0) {
      for (size_t i = 0; i < entries.size(); i++) {
        const auto& source = raw_entries[i];
        if (!source.revents) {
          continue;
        }

        auto& dest = entries[i];

        const auto set_event = [&](int raw_flag, StatusEvents flag) {
          if (source.revents & raw_flag) {
            dest.status_events = dest.status_events | flag;
          }
        };

        set_event(POLLERR, StatusEvents::Error);
        set_event(POLLHUP, StatusEvents::Disconnected);
        set_event(POLLNVAL, StatusEvents::InvalidSocket);
        set_event(POLLIN, StatusEvents::CanReceiveFrom);
        set_event(POLLOUT, StatusEvents::CanSendTo);
      }
    }

    return {
      .status = {},
      .value = signaled_entries,
    };
  }

  bool cancel() override {
    if (!cancel_pending.exchange(true)) {
      return canceller.signal();
    }
    return true;
  }
};

std::unique_ptr<sock::Poller> sock::Poller::create() {
  auto impl = std::make_unique<PollerImpl>();
  if (!*impl) {
    return nullptr;
  }
  return impl;
}
bool sock::Poller::PollEntry::has_events(StatusEvents events) const {
  return (status_events & events) == events;
}
bool sock::Poller::PollEntry::has_any_event(StatusEvents events) const {
  return (status_events & events) != StatusEvents::None;
}

bool sock::initialize() {
#if defined(SOCKLIB_WINDOWS)
  class InitializationGuard {
    bool status_ = false;

   public:
    InitializationGuard() {
      WSAData wsa_data{};
      status_ = WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
    }

    bool status() const { return status_; }
  };
  static InitializationGuard guard;
  return guard.status();
#else
  return true;
#endif
}
