#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "Address.hpp"
#include "Status.hpp"

#define SOCKLIB_IMPLEMENT_ENUM_BIT_OPERATIONS(type) \
  constexpr inline type operator|(type a, type b) { \
    using T = std::underlying_type_t<type>;         \
    return type(T(a) | T(b));                       \
  }                                                 \
  constexpr inline type operator&(type a, type b) { \
    using T = std::underlying_type_t<type>;         \
    return type(T(a) & T(b));                       \
  }                                                 \
  constexpr inline type operator~(type a) {         \
    using T = std::underlying_type_t<type>;         \
    return type(~T(a));                             \
  }

namespace sock {

namespace detail {

#if defined(_WIN32)
using RawSocket = uintptr_t;
constexpr RawSocket invalid_raw_socket = static_cast<RawSocket>(~static_cast<uintptr_t>(0));
#else
using RawSocket = int;
constexpr RawSocket invalid_raw_socket = -1;
#endif

struct RawSocketAccessor;

}  // namespace detail

struct IpResolver {
  static Result<IpV4Address> resolve_ipv4(std::string_view hostname);
  static Result<IpV6Address> resolve_ipv6(std::string_view hostname);
};

class Socket {
  friend class detail::RawSocketAccessor;

 protected:
  detail::RawSocket raw_socket_{detail::invalid_raw_socket};

  explicit Socket(detail::RawSocket raw_socket);

 public:
  Socket() = default;

  Socket(Socket&& other) noexcept;
  Socket& operator=(Socket&& other) noexcept;

  Socket(const Socket& other) = delete;
  Socket& operator=(const Socket& other) = delete;

  ~Socket();

  bool valid() const;
  operator bool() const { return valid(); }

  Status set_non_blocking(bool non_blocking);

  Status local_address(SocketAddress& address) const;
  Status peer_address(SocketAddress& address) const;

  template <typename T>
  Result<T> local_address() const {
    T address{};
    const auto status = local_address(address);
    return {.status = status, .address = address};
  }

  template <typename T>
  Result<T> peer_address() const {
    T address{};
    const auto status = peer_address(address);
    return {.status = status, .address = address};
  }
};

namespace detail {

class RwSocket : public Socket {
 protected:
  using Socket::Socket;

 public:
  Status set_receive_timeout_ms(uint64_t timeout_ms);
  Status set_send_timeout_ms(uint64_t timeout_ms);
  Status set_broadcast_enabled(bool broadcast_enabled);
};

}  // namespace detail

class DatagramSocket : public detail::RwSocket {
  using RwSocket::RwSocket;

 public:
  struct BindParameters {
    bool reuse_address = false;

    constexpr static BindParameters default_parameters() { return BindParameters{}; }
  };
  static Result<DatagramSocket> bind(
    const SocketAddress& address,
    const BindParameters& bind_parameters = BindParameters::default_parameters());
  static Result<DatagramSocket> bind(
    IpVersion ip_version,
    std::string_view hostname,
    uint16_t port,
    const BindParameters& bind_parameters = BindParameters::default_parameters());

  struct CreateParameters {
    constexpr static CreateParameters default_parameters() { return CreateParameters{}; }
  };
  static Result<DatagramSocket> create(
    SocketAddress::Type type,
    const CreateParameters& create_parameters = CreateParameters::default_parameters());

  Result<size_t> send(const SocketAddress& to, const void* data, size_t data_size);
  Result<size_t> send_all(const SocketAddress& to, const void* data, size_t data_size);
  Result<size_t> receive(SocketAddress& from, void* data, size_t data_size);

  Result<size_t> send(const SocketAddress& to, std::span<const uint8_t> data) {
    return send(to, data.data(), data.size());
  }
  Result<size_t> send_all(const SocketAddress& to, std::span<const uint8_t> data) {
    return send_all(to, data.data(), data.size());
  }
  Result<size_t> receive(SocketAddress& from, std::span<uint8_t> data) {
    return receive(from, data.data(), data.size());
  }
};

class StreamSocket : public detail::RwSocket {
  friend class Listener;

  using RwSocket::RwSocket;

 public:
  struct ConnectParameters {
    constexpr static ConnectParameters default_parameters() { return ConnectParameters{}; }
  };
  static Result<StreamSocket> connect(
    const SocketAddress& address,
    const ConnectParameters& connect_parameters = ConnectParameters::default_parameters());
  static Result<StreamSocket> connect(
    IpVersion ip_version,
    std::string_view hostname,
    uint16_t port,
    const ConnectParameters& connect_parameters = ConnectParameters::default_parameters());

  Result<size_t> send(const void* data, size_t data_size);
  Result<size_t> send_all(const void* data, size_t data_size);
  Result<size_t> receive(void* data, size_t data_size);

  Result<size_t> send(std::span<const uint8_t> data) { return send(data.data(), data.size()); }
  Result<size_t> send_all(std::span<const uint8_t> data) {
    return send_all(data.data(), data.size());
  }
  Result<size_t> receive(std::span<uint8_t> data) { return receive(data.data(), data.size()); }
};

class Listener : public Socket {
  using Socket::Socket;

 public:
  struct BindParameters {
    bool reuse_address = false;
    int max_pending_connections = 16;

    constexpr static BindParameters default_parameters() { return BindParameters{}; }
  };
  static Result<Listener> bind(
    const SocketAddress& address,
    const BindParameters& bind_parameters = BindParameters::default_parameters());
  static Result<Listener> bind(
    IpVersion ip_version,
    std::string_view hostname,
    uint16_t port,
    const BindParameters& bind_parameters = BindParameters::default_parameters());

  Result<StreamSocket> accept(SocketAddress* peer_address = nullptr);
};

class Poller {
 public:
  static std::unique_ptr<Poller> create();

  virtual ~Poller() = default;

  enum class QueryEvents {
    None = 0,
    CanReceiveFrom = (1 << 0),
    CanSendTo = (1 << 1),
    CanAccept = CanReceiveFrom,
  };

  enum class StatusEvents {
    None = 0,
    Error = (1 << 0),
    Disconnected = (1 << 1),
    InvalidSocket = (1 << 2),
    CanReceiveFrom = (1 << 3),
    CanSendTo = (1 << 4),
    CanAccept = CanReceiveFrom,
  };

  struct PollEntry {
    const Socket* socket{};
    QueryEvents query_events{};
    StatusEvents status_events{};

    bool has_all_status_events(StatusEvents events) const;
    bool has_one_of_status_events(StatusEvents events) const;
  };

  virtual Result<size_t> poll(std::span<PollEntry> entries, int timeout_ms) = 0;
};

SOCKLIB_IMPLEMENT_ENUM_BIT_OPERATIONS(Poller::QueryEvents)
SOCKLIB_IMPLEMENT_ENUM_BIT_OPERATIONS(Poller::StatusEvents)

[[nodiscard]] bool initialize();

}  // namespace sock

#undef SOCKLIB_IMPLEMENT_ENUM_BIT_OPERATIONS