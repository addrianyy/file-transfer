#include "Socket.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

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

#include <afunix.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct SockaddrBuffer {
  alignas(32) uint8_t data[sizeof(sockaddr_storage)];
};

static bool initialize_sockets() {
#if defined(SOCKLIB_WINDOWS)
  class InitializationGuard {
    bool status_ = false;

   public:
    InitializationGuard() {
      WSAData wsa_data{};
      const auto version = MAKEWORD(2, 2);
      status_ = WSAStartup(version, &wsa_data) == 0;
      if (wsa_data.wVersion != version) {
        status_ = false;
      }
    }

    bool status() const { return status_; }
  };
  static InitializationGuard guard;
  return guard.status();
#else
  return true;
#endif
}

#if defined(SOCKLIB_WINDOWS)
#define ENSURE_INITIALIZED()                                                 \
  if (!initialize_sockets()) {                                               \
    return {.status = {sock::Error::InitializationFailed, sock::Error::None, \
                       sock::SystemError::NotInitialized}};                  \
  }
#else
#define ENSURE_INITIALIZED()
#endif

static int address_type_to_protocol(sock::SocketAddress::Type type) {
  switch (type) {
    case sock::SocketAddress::Type::IpV4:
      return AF_INET;
    case sock::SocketAddress::Type::IpV6:
      return AF_INET6;
    case sock::SocketAddress::Type::Unix:
      return AF_UNIX;
    default:
      return AF_MAX;
  }
}

constexpr static size_t sockaddr_un_max_path_size = sizeof(::sockaddr_un{}.sun_path);
static_assert(sockaddr_un_max_path_size >= (sock::SocketUnixAddress::max_path_size + 1),
              "unix socket path is shorter than expected");
constexpr static size_t sockaddr_un_header_size =
  sizeof(::sockaddr_un{}) - sockaddr_un_max_path_size;

template <typename Fn>
static void socket_address_convert_to_raw(const sock::SocketAddress& address, Fn&& callback) {
  switch (address.type()) {
    case sock::SocketAddress::Type::IpV4: {
      const auto& address_ipv4 = static_cast<const sock::SocketIpV4Address&>(address);
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
      const auto& address_ipv6 = static_cast<const sock::SocketIpV6Address&>(address);
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

    case sock::SocketAddress::Type::Unix: {
      const auto& address_unix = static_cast<const sock::SocketUnixAddress&>(address);
      const auto address_path = address_unix.path();

      sockaddr_un sockaddr_un{};
      sockaddr_un.sun_family = AF_UNIX;

      {
        // We have zeroed the sockaddr_un structure so we don't need to null terminate or set zero
        // byte at the beginning.
        const auto offset =
          address_unix.socket_namespace() == sock::SocketUnixAddress::Namespace::Abstract ? 1 : 0;
        std::memcpy(sockaddr_un.sun_path + offset, address_path.data(), address_path.size());
      }

      // Always add one because we are either null terminating or adding 0 prefix for abstract
      // sockets.
      const auto address_path_bytes = address_path.size() + 1;

      callback(reinterpret_cast<const sockaddr*>(&sockaddr_un),
               socklen_t(sockaddr_un_header_size + address_path_bytes));

      break;
    }

    default:
      break;
  }
}

static bool socket_address_convert_from_raw(const sockaddr* sockaddr_buffer,
                                            socklen_t sockaddr_size,
                                            sock::SocketAddress& address) {
  switch (address.type()) {
    case sock::SocketAddress::Type::IpV4: {
      if (sockaddr_buffer->sa_family != AF_INET || sockaddr_size < sizeof(sockaddr_in)) {
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
      if (sockaddr_buffer->sa_family != AF_INET6 || sockaddr_size < sizeof(sockaddr_in6)) {
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

    case sock::SocketAddress::Type::Unix: {
      // Fail on zero sized path.
      if (sockaddr_buffer->sa_family != AF_UNIX || sockaddr_size <= sockaddr_un_header_size) {
        return false;
      }

      // Always > 0.
      const auto unix_path_buffer =
        std::span<const char>(reinterpret_cast<const sockaddr_un*>(sockaddr_buffer)->sun_path,
                              size_t(sockaddr_size - sockaddr_un_header_size));

      sock::SocketUnixAddress::Namespace socket_namespace{};
      std::string_view unix_path{};

      if (unix_path_buffer[0] == 0) {
        const auto actual_path = unix_path_buffer.subspan(1);

        socket_namespace = sock::SocketUnixAddress::Namespace::Abstract;
        unix_path = std::string_view{actual_path.data(), actual_path.size()};
      } else {
        socket_namespace = sock::SocketUnixAddress::Namespace::Filesystem;
        unix_path = std::string_view{unix_path_buffer.data(), unix_path_buffer.size()};

        const auto null_terminator = unix_path.find_first_of('\0');
        if (null_terminator != std::string_view::npos) {
          unix_path = unix_path.substr(0, null_terminator);
        }
      }

      const auto conveted_address = sock::SocketUnixAddress::create(socket_namespace, unix_path);
      if (conveted_address) {
        *reinterpret_cast<sock::SocketUnixAddress*>(&address) = *conveted_address;
      } else {
        return false;
      }
    }

    default:
      return false;
  }
}

static bool socket_address_convert_from_raw(const SockaddrBuffer& sockaddr_buffer,
                                            socklen_t sockaddr_size,
                                            sock::SocketAddress& address) {
  return socket_address_convert_from_raw(reinterpret_cast<const sockaddr*>(sockaddr_buffer.data),
                                         sockaddr_size, address);
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

static sock::SystemError error_to_system_error(int error_num) {
  using EC = sock::SystemError;

#if defined(SOCKLIB_WINDOWS)
  switch (error_num) {
    case WSAEISCONN:
      return EC::AlreadyConnected;
    case WSAENOTCONN:
      return EC::NotConnected;
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
  switch (error_num) {
    case EISCONN:
      return EC::AlreadyConnected;
    case ENOTCONN:
      return EC::NotConnected;
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

static sock::SystemError last_error_to_system_error() {
#if defined(SOCKLIB_WINDOWS)
  return error_to_system_error(WSAGetLastError());
#else
  return error_to_system_error(errno);
#endif
}

static sock::Status last_error_to_status(sock::Error error) {
  return sock::Status{error, sock::Error::None, last_error_to_system_error()};
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
    return last_error_to_status(sock::Error::SetSocketOptionFailed);
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
  if (timeout_ms > std::numeric_limits<uint32_t>::max()) {
    return sock::Status{sock::Error::SetSocketOptionFailed, sock::Error::TimeoutTooLarge};
  }
  return set_socket_option<uint32_t>(socket, level, option, timeout_ms);
#else
  timeval timeout_timeval{};
  timeout_timeval.tv_sec = static_cast<long>(timeout_ms / 1000);
  timeout_timeval.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
  return set_socket_option<timeval>(socket, level, option, timeout_timeval);
#endif
}

static sock::Status set_socket_non_blocking(sock::detail::RawSocket socket, bool non_blocking) {
#if defined(SOCKLIB_WINDOWS)
  auto non_blocking_value = static_cast<u_long>(non_blocking);
  if (is_error(::ioctlsocket(socket, FIONBIO, &non_blocking_value))) {
    return last_error_to_status(sock::Error::SetSocketBlockingFailed);
  }
  return {};
#else
  if (is_error(
        ::fcntl(socket, F_SETFL,
                (non_blocking ? O_NONBLOCK : 0) | (::fcntl(socket, F_GETFL) & ~O_NONBLOCK)))) {
    return last_error_to_status(sock::Error::SetSocketBlockingFailed);
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

template <typename TSocketAddress>
static sock::Result<std::vector<typename TSocketAddress::Ip>> resolve_ip_generic(
  int family,
  std::string_view hostname) {
  ENSURE_INITIALIZED();

  addrinfo* resolved{};
  addrinfo hints{};

  hints.ai_family = family;
  hints.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_ALL;

  const auto hostname_s = std::string(hostname);

  // Different return value semantics, check if != 0.
  const auto addrinfo_result = ::getaddrinfo(hostname_s.c_str(), nullptr, &hints, &resolved);
  if (addrinfo_result != 0) {
    return {
      .status = {sock::Error::HostnameNotFound},
    };
  }

  std::vector<typename TSocketAddress::Ip> ips;

  for (auto current = resolved; current; current = current->ai_next) {
    if (current->ai_family != hints.ai_family) {
      continue;
    }

    constexpr bool is_ipv4 = std::is_same_v<TSocketAddress, sock::SocketIpV4Address>;
    constexpr bool is_ipv6 = std::is_same_v<TSocketAddress, sock::SocketIpV6Address>;
    static_assert(is_ipv4 || is_ipv6, "invalid address type");

    socklen_t sockaddr_size{};
    if constexpr (is_ipv4) {
      reinterpret_cast<sockaddr_in*>(current->ai_addr)->sin_port = 0;
      sockaddr_size = sizeof(sockaddr_in);
    } else if constexpr (is_ipv6) {
      reinterpret_cast<sockaddr_in6*>(current->ai_addr)->sin6_port = 0;
      sockaddr_size = sizeof(sockaddr_in6);
    }

    TSocketAddress address;
    if (socket_address_convert_from_raw(current->ai_addr, sockaddr_size, address)) {
      ips.push_back(address.ip());
    }
  }

  ::freeaddrinfo(resolved);

  if (ips.empty()) {
    return {
      .status = {sock::Error::HostnameNotFound},
    };
  } else {
    return {
      .value = std::move(ips),
    };
  }
}

sock::Result<std::vector<sock::IpV4Address>> sock::IpResolver::resolve_ipv4(
  std::string_view hostname) {
  return resolve_ip_generic<SocketIpV4Address>(AF_INET, hostname);
}

sock::Result<std::vector<sock::IpV6Address>> sock::IpResolver::resolve_ipv6(
  std::string_view hostname) {
  return resolve_ip_generic<SocketIpV6Address>(AF_INET6, hostname);
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
      sock::Status error_status{};
      for (const auto& ip : resolved.value) {
        auto result = callback(sock::SocketIpV4Address(ip, port));
        if (result) {
          return result;
        }
        if (error_status) {
          error_status = result.status;
        }
      }
      return {
        .status = error_status,
      };
    }
    case sock::IpVersion::V6: {
      const auto resolved = sock::IpResolver::resolve_ipv6(hostname);
      if (!resolved) {
        return {
          .status = wrap_status(resolved.status, sock::Error::IpResolveFailed),
        };
      }
      sock::Status error_status{};
      for (const auto& ip : resolved.value) {
        auto result = callback(sock::SocketIpV6Address(ip, port));
        if (result) {
          return result;
        }
        if (error_status) {
          error_status = result.status;
        }
      }
      return {
        .status = error_status,
      };
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
  socklen_t sockaddr_size = sizeof(socket_address);

  if (is_error(::getsockname(raw_socket_, reinterpret_cast<sockaddr*>(socket_address.data),
                             &sockaddr_size))) {
    return last_error_to_status(Error::GetLocalAddressFailed);
  }

  if (!socket_address_convert_from_raw(socket_address, sockaddr_size, address)) {
    return Status{Error::GetLocalAddressFailed, Error::AddressConversionFailed};
  }

  return {};
}

sock::SystemError sock::Socket::last_error() {
  int error{};
  socklen_t error_length = sizeof(error);
  if (::getsockopt(raw_socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error),
                   &error_length) != 0) {
    return sock::SystemError::Unknown;
  }

  if (error == 0) {
    return sock::SystemError::None;
  }

  return error_to_system_error(error);
}

sock::Status sock::detail::RwSocket::set_receive_timeout_ms(uint64_t timeout_ms) {
  return set_socket_option_timeout_ms(raw_socket_, SOL_SOCKET, SO_RCVTIMEO, timeout_ms);
}

sock::Status sock::detail::RwSocket::set_send_timeout_ms(uint64_t timeout_ms) {
  return set_socket_option_timeout_ms(raw_socket_, SOL_SOCKET, SO_SNDTIMEO, timeout_ms);
}

sock::Status sock::detail::RwSocket::set_receive_buffer_size(size_t size) {
  if (size > std::numeric_limits<int>::max()) {
    return {Error::SizeTooLarge};
  }
  return set_socket_option<int>(raw_socket_, SOL_SOCKET, SO_RCVBUF, int(size));
}

sock::Status sock::detail::RwSocket::set_send_buffer_size(size_t size) {
  if (size > std::numeric_limits<int>::max()) {
    return {Error::SizeTooLarge};
  }
  return set_socket_option<int>(raw_socket_, SOL_SOCKET, SO_SNDBUF, int(size));
}

sock::Result<size_t> sock::DatagramSocket::send_to_internal(const sock::SocketAddress* to,
                                                            const void* data,
                                                            size_t data_size) {
  if (data_size > std::numeric_limits<int>::max()) {
    return {
      .status = {Error::SendFailed, Error::SizeTooLarge},
    };
  }

  intptr_t result{default_error_value};
  if (to) {
    socket_address_convert_to_raw(
      *to, [&](const sockaddr* socket_address, socklen_t sockaddr_size) {
        result = handle_eintr([&] {
          return ::sendto(raw_socket_, reinterpret_cast<const char*>(data), data_size, MSG_NOSIGNAL,
                          socket_address, sockaddr_size);
        });
      });
  } else {
    result = handle_eintr([&] {
      return ::send(raw_socket_, reinterpret_cast<const char*>(data), data_size, MSG_NOSIGNAL);
    });
  }

  if (is_error_ext(result)) {
    return {
      .status = last_error_to_status(Error::SendFailed),
    };
  }

  return {
    .status = {},
    .value = size_t(result),
  };
}

sock::Result<size_t> sock::DatagramSocket::receive_from_internal(sock::SocketAddress* from,
                                                                 void* data,
                                                                 size_t data_size) {
  if (data_size > std::numeric_limits<int>::max()) {
    return {
      .status = {Error::ReceiveFailed, Error::SizeTooLarge},
    };
  }

  SockaddrBuffer socket_address;
  socklen_t sockaddr_size = sizeof(socket_address);

  intptr_t result = 0;
  if (from) {
    result = handle_eintr([&] {
      return ::recvfrom(raw_socket_, reinterpret_cast<char*>(data), data_size, 0,
                        reinterpret_cast<sockaddr*>(socket_address.data), &sockaddr_size);
    });
  } else {
    result = handle_eintr(
      [&] { return ::recv(raw_socket_, reinterpret_cast<char*>(data), data_size, 0); });
  }

  if (is_error_ext(result)) {
    return {
      .status = last_error_to_status(Error::ReceiveFailed),
    };
  }

  if (from) {
    if (!socket_address_convert_from_raw(socket_address, sockaddr_size, *from)) {
      return {
        .status = {Error::ReceiveFailed, Error::AddressConversionFailed},
      };
    }
  }

  return {
    .status = {},
    .value = size_t(result),
  };
}

sock::Result<sock::DatagramSocket> sock::DatagramSocket::bind(
  const SocketAddress& address,
  const BindParameters& bind_parameters) {
  ENSURE_INITIALIZED();

  const auto datagram_socket = ::socket(address_type_to_protocol(address.type()), SOCK_DGRAM, 0);
  if (!is_valid_socket(datagram_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed),
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
    const auto status = last_error_to_status(Error::BindFailed);
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
  ENSURE_INITIALIZED();

  const auto datagram_socket = ::socket(address_type_to_protocol(type), SOCK_DGRAM, 0);
  if (!is_valid_socket(datagram_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed),
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

sock::Result<sock::DatagramSocket> sock::DatagramSocket::connect(
  const sock::SocketAddress& address,
  const sock::DatagramSocket::ConnectParameters& connect_parameters) {
  ENSURE_INITIALIZED();

  const auto datagram_socket = ::socket(address_type_to_protocol(address.type()), SOCK_DGRAM, 0);
  if (!is_valid_socket(datagram_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed),
    };
  }

  // Set non-blocking state later after we estabilish the connection.
  const auto setup_status = setup_socket(datagram_socket, false, false, false);
  if (!setup_status) {
    close_socket_if_valid(datagram_socket);
    return {
      .status = setup_status,
    };
  }

  int connect_status{default_error_value};
  socket_address_convert_to_raw(address, [&](const sockaddr* sockaddr, socklen_t sockaddr_size) {
    connect_status =
      handle_eintr([&] { return ::connect(datagram_socket, sockaddr, sockaddr_size); });
  });
  if (is_error(connect_status)) {
    const auto status = last_error_to_status(Error::ConnectFailed);
    close_socket_if_valid(connect_status);
    return {
      .status = status,
    };
  }

  if (connect_parameters.non_blocking) {
    const auto non_blocking_status = set_socket_non_blocking(datagram_socket, true);
    if (!non_blocking_status) {
      close_socket_if_valid(datagram_socket);
      return {
        .status = wrap_status(non_blocking_status, Error::ConnectFailed),
      };
    }
  }

  return {
    .status = {},
    .value = DatagramSocket{datagram_socket},
  };
}

sock::Result<sock::DatagramSocket> sock::DatagramSocket::connect(
  sock::IpVersion ip_version,
  std::string_view hostname,
  uint16_t port,
  const sock::DatagramSocket::ConnectParameters& connect_parameters) {
  return resolve_and_run<Result<DatagramSocket>>(
    ip_version, hostname, port, [&](const SocketAddress& resolved_address) {
      return connect(resolved_address, connect_parameters);
    });
}

sock::Status sock::DatagramSocket::set_broadcast_enabled(bool broadcast_enabled) {
  return set_socket_option<int>(raw_socket_, SOL_SOCKET, SO_BROADCAST, broadcast_enabled ? 1 : 0);
}

sock::Result<size_t> sock::DatagramSocket::send_to(const SocketAddress& to,
                                                   const void* data,
                                                   size_t data_size) {
  return send_to_internal(&to, data, data_size);
}

sock::Result<size_t> sock::DatagramSocket::receive_from(SocketAddress& from,
                                                        void* data,
                                                        size_t data_size) {
  return receive_from_internal(&from, data, data_size);
}

sock::Result<size_t> sock::DatagramSocket::send(const void* data, size_t data_size) {
  return send_to_internal(nullptr, data, data_size);
}

sock::Result<size_t> sock::DatagramSocket::receive(void* data, size_t data_size) {
  return receive_from_internal(nullptr, data, data_size);
}

sock::Result<sock::StreamSocket> sock::StreamSocket::connect(
  const SocketAddress& address,
  const ConnectParameters& connect_parameters) {
  ENSURE_INITIALIZED();

  const auto connection_socket = ::socket(address_type_to_protocol(address.type()), SOCK_STREAM, 0);
  if (!is_valid_socket(connection_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed),
    };
  }

  // Set non-blocking state later after we estabilish the connection.
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
    const auto status = last_error_to_status(Error::ConnectFailed);
    close_socket_if_valid(connect_status);
    return {
      .status = status,
    };
  }

  if (connect_parameters.non_blocking) {
    const auto non_blocking_status = set_socket_non_blocking(connection_socket, true);
    if (!non_blocking_status) {
      close_socket_if_valid(connection_socket);
      return {
        .status = wrap_status(non_blocking_status, Error::ConnectFailed),
      };
    }
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

  auto [connect_status, connection] = sock::ConnectingStreamSocket::initiate_connection(
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
  ENSURE_INITIALIZED();

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
      .status = last_error_to_status(Error::SocketPairFailed),
    };
  }
  return {.status = {}, .value = {StreamSocket{sockets[0]}, StreamSocket{sockets[1]}}};
#endif
}

sock::Status sock::StreamSocket::peer_address(SocketAddress& address) const {
  SockaddrBuffer socket_address;
  socklen_t sockaddr_size = sizeof(socket_address);

  if (is_error(::getpeername(raw_socket_, reinterpret_cast<sockaddr*>(socket_address.data),
                             &sockaddr_size))) {
    return last_error_to_status(Error::GetPeerAddressFailed);
  }

  if (!socket_address_convert_from_raw(socket_address, sockaddr_size, address)) {
    return Status{Error::GetPeerAddressFailed, Error::AddressConversionFailed};
  }

  return {};
}

sock::Status sock::StreamSocket::set_keep_alive(bool keep_alive_enabled) {
  return set_socket_option<int>(raw_socket_, SOL_SOCKET, SO_KEEPALIVE, keep_alive_enabled ? 1 : 0);
}

sock::Status sock::StreamSocket::set_no_delay(bool no_delay_enabled) {
  return set_socket_option<int>(raw_socket_, IPPROTO_TCP, TCP_NODELAY, no_delay_enabled ? 1 : 0);
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
      .status = last_error_to_status(Error::SendFailed),
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
      .status = last_error_to_status(Error::ReceiveFailed),
    };
  }

  return {
    .status = {},
    .value = size_t(result),
  };
}

sock::Result<size_t> sock::StreamSocket::receive_exact(void* data, size_t data_size) {
  auto current = reinterpret_cast<uint8_t*>(data);
  size_t bytes_received = 0;

  while (bytes_received < data_size) {
    const auto [status, current_received] = receive(current, data_size - bytes_received);
    if (!status) {
      return {
        .status = status,
        .value = bytes_received,
      };
    }
    current += current_received;
    bytes_received += current_received;
  }

  return {
    .status = {},
    .value = bytes_received,
  };
}

sock::ConnectingStreamSocket::ConnectingStreamSocket(detail::RawSocket raw_socket,
                                                     std::unique_ptr<uint64_t[]> socket_address,
                                                     size_t socket_address_size)
    : Socket(raw_socket),
      socket_address(std::move(socket_address)),
      socket_address_size(socket_address_size) {}

sock::ConnectingStreamSocket::ConnectingStreamSocket(ConnectingStreamSocket&& other) noexcept {
  raw_socket_ = other.raw_socket_;
  socket_address = std::move(other.socket_address);
  socket_address_size = other.socket_address_size;

  other.raw_socket_ = detail::invalid_raw_socket;
  other.socket_address = {};
  other.socket_address_size = 0;
}

sock::ConnectingStreamSocket& sock::ConnectingStreamSocket::operator=(
  sock::ConnectingStreamSocket&& other) noexcept {
  if (this != &other) {
    close_socket_if_valid(raw_socket_);

    raw_socket_ = other.raw_socket_;
    socket_address = std::move(other.socket_address);
    socket_address_size = other.socket_address_size;

    other.raw_socket_ = detail::invalid_raw_socket;
    other.socket_address = {};
    other.socket_address_size = 0;
  }

  return *this;
}

sock::Result<sock::ConnectingStreamSocket::SocketPair>
sock::ConnectingStreamSocket::initiate_connection(const SocketAddress& address,
                                                  const ConnectParameters& connect_parameters) {
  ENSURE_INITIALIZED();

  const auto connection_socket = ::socket(address_type_to_protocol(address.type()), SOCK_STREAM, 0);
  if (!is_valid_socket(connection_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed),
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
                     int(socket_address_size));
  });

  if (is_error(connect_status)) {
    const auto status = last_error_to_status(Error::ConnectFailed);
    if (!status.would_block() && !status.has_error(SystemError::AlreadyInProgress) &&
        !status.has_error(SystemError::NowInProgress)) {
      close_socket_if_valid(connect_status);
      return {
        .status = status,
      };
    }

    return {
      .status = {},
      .value = {ConnectingStreamSocket{connection_socket, std::move(socket_address_buffer),
                                       socket_address_size},
                StreamSocket{}},
    };
  } else {
    return {
      .status = {},
      .value = {ConnectingStreamSocket{}, StreamSocket{connection_socket}},
    };
  }
}

sock::Result<sock::StreamSocket> sock::ConnectingStreamSocket::connect() {
  if (raw_socket_ == detail::invalid_raw_socket) {
    return {
      .status = Status{Error::ConnectFailed, Error::None, SystemError::None},
    };
  }

  const int connect_status = handle_eintr([&] {
    return ::connect(raw_socket_, reinterpret_cast<sockaddr*>(socket_address.get()),
                     int(socket_address_size));
  });

  if (is_error(connect_status)) {
    auto status = last_error_to_status(Error::ConnectFailed);
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
  socket_address_size = 0;

  return {.status = {}, .value = StreamSocket{socket}};
}

sock::Result<sock::Listener> sock::Listener::bind(const SocketAddress& address,
                                                  const BindParameters& bind_parameters) {
  ENSURE_INITIALIZED();

  const auto listener_socket = ::socket(address_type_to_protocol(address.type()), SOCK_STREAM, 0);
  if (!is_valid_socket(listener_socket)) {
    return {
      .status = last_error_to_status(Error::SocketCreationFailed),
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
    const auto status = last_error_to_status(Error::BindFailed);
    close_socket_if_valid(listener_socket);
    return {
      .status = status,
    };
  }

  const auto backlog_size = std::min(int(bind_parameters.max_pending_connections), SOMAXCONN);
  if (is_error(::listen(listener_socket, backlog_size))) {
    const auto status = last_error_to_status(Error::ListenFailed);
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
  socklen_t sockaddr_size = sizeof(socket_address);

  const detail::RawSocket accepted_socket = handle_eintr([&]() -> detail::RawSocket {
    if (peer_address) {
      return ::accept(raw_socket_, reinterpret_cast<sockaddr*>(socket_address.data),
                      &sockaddr_size);
    } else {
      return ::accept(raw_socket_, nullptr, nullptr);
    }
  });

  if (!is_valid_socket(accepted_socket)) {
    return {
      .status = last_error_to_status(Error::AcceptFailed),
    };
  }

  if (peer_address) {
    if (!socket_address_convert_from_raw(socket_address, sockaddr_size, *peer_address)) {
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

  std::unique_ptr<PollCanceller> canceller;
  std::atomic_bool cancel_pending{false};

 public:
  explicit PollerImpl(const CreateParameters& create_parameters) {
    if (create_parameters.enable_cancellation) {
      canceller = std::make_unique<PollCanceller>();
    }
  }

  operator bool() const {
    if (canceller && !canceller->valid()) {
      return false;
    }
    return true;
  }

  sock::Result<size_t> poll(std::span<PollEntry> entries, int timeout_ms) override {
    if (entries.empty() && !canceller) {
      if (timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
      } else if (timeout_ms < 0) {
        std::this_thread::sleep_for(std::chrono::hours(24));
      }
      return {
        .status = {},
        .value = 0,
      };
    }

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

    if (canceller) {
      if (cancel_pending) {
        if (canceller->drain()) {
          cancel_pending = false;
          return {
            .status = {},
            .value = {},
          };
        }
      }

      raw_entries.push_back({
        .fd = canceller->cancel_socket(),
        .events = POLLIN,
      });
    }

#if defined(SOCKLIB_WINDOWS)
    const auto poll_result = handle_eintr(
      [&] { return ::WSAPoll(raw_entries.data(), ULONG(raw_entries.size()), timeout_ms); });
#else
    const auto poll_result =
      handle_eintr([&] { return ::poll(raw_entries.data(), int(raw_entries.size()), timeout_ms); });
#endif

    if (is_error(poll_result)) {
      return {
        .status = last_error_to_status(sock::Error::PollFailed),
      };
    }

    auto signaled_entries = size_t(poll_result);

    if (canceller) {
      if (const auto& last = raw_entries[entries.size()]; last.revents) {
        const bool signaled_input = last.revents & POLLIN;
        bool cancellation_error = last.revents & (POLLERR | POLLHUP | POLLNVAL);

        if (signaled_input) {
          if (canceller->drain()) {
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
    if (canceller) {
      if (!cancel_pending.exchange(true)) {
        return canceller->signal();
      }
      return true;
    } else {
      return false;
    }
  }
};

std::unique_ptr<sock::Poller> sock::Poller::create(const CreateParameters& create_parameters) {
  if (!initialize_sockets()) {
    return nullptr;
  }
  auto impl = std::make_unique<PollerImpl>(create_parameters);
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
