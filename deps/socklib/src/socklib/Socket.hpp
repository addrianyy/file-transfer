#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "Address.hpp"
#include "Status.hpp"

#define SOCKLIB_NON_COPYABLE_NON_MOVABLE(target) \
  target(const target&) = delete;                \
  target& operator=(const target&) = delete;     \
  target(target&&) = delete;                     \
  target& operator=(target&&) = delete;

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
}  // namespace detail

struct IpResolver {
  struct ResolveIpV4Result {
    Status status{};
    IpV4Address address{};

    constexpr operator bool() const { return status.success(); }
  };
  [[nodiscard]] static ResolveIpV4Result resolve_ipv4(std::string_view hostname);

  struct ResolveIpV6Result {
    Status status{};
    IpV6Address address{};

    constexpr operator bool() const { return status.success(); }
  };
  [[nodiscard]] static ResolveIpV6Result resolve_ipv6(std::string_view hostname);
};

class Socket {
 protected:
  detail::RawSocket raw_socket_{detail::invalid_raw_socket};

  explicit Socket(detail::RawSocket raw_socket);

 public:
  SOCKLIB_NON_COPYABLE_NON_MOVABLE(Socket)

  ~Socket();

  [[nodiscard]] detail::RawSocket raw_socket() const { return raw_socket_; }

  [[nodiscard]] Status set_non_blocking(bool non_blocking);
};

namespace detail {

class RwSocket : public Socket {
 protected:
  using Socket::Socket;

 public:
  SOCKLIB_NON_COPYABLE_NON_MOVABLE(RwSocket)

  [[nodiscard]] Status set_receive_timeout_ms(uint64_t timeout_ms);
  [[nodiscard]] Status set_send_timeout_ms(uint64_t timeout_ms);
  [[nodiscard]] Status set_broadcast_enabled(bool broadcast_enabled);
};

}  // namespace detail

class SocketDatagram : public detail::RwSocket {
  using RwSocket::RwSocket;

 public:
  SOCKLIB_NON_COPYABLE_NON_MOVABLE(SocketDatagram)

  struct BindResult {
    Status status{};
    std::unique_ptr<SocketDatagram> datagram{};

    constexpr operator bool() const { return status.success(); }
  };
  struct BindParameters {
    bool reuse_address = false;

    constexpr static BindParameters default_parameters() { return BindParameters{}; }
  };
  [[nodiscard]] static BindResult bind(
    const SocketAddress& address,
    const BindParameters& bind_parameters = BindParameters::default_parameters());
  [[nodiscard]] static BindResult bind(
    IpVersion ip_version,
    std::string_view hostname,
    uint16_t port,
    const BindParameters& bind_parameters = BindParameters::default_parameters());

  struct CreateResult {
    Status status{};
    std::unique_ptr<SocketDatagram> datagram{};

    constexpr operator bool() const { return status.success(); }
  };
  struct CreateParameters {
    constexpr static CreateParameters default_parameters() { return CreateParameters{}; }
  };
  [[nodiscard]] static CreateResult create(
    SocketAddress::Type type,
    const CreateParameters& create_parameters = CreateParameters::default_parameters());

  struct SendReceiveResult {
    Status status{};
    size_t byte_count{};

    constexpr operator bool() const { return status.success(); }
  };
  [[nodiscard]] SendReceiveResult send(const SocketAddress& to, const void* data, size_t data_size);
  [[nodiscard]] SendReceiveResult send_all(const SocketAddress& to,
                                           const void* data,
                                           size_t data_size);
  [[nodiscard]] SendReceiveResult receive(SocketAddress& from, void* data, size_t data_size);

  [[nodiscard]] SendReceiveResult send(const SocketAddress& to, std::span<const uint8_t> data) {
    return send(to, data.data(), data.size());
  }
  [[nodiscard]] SendReceiveResult send_all(const SocketAddress& to, std::span<const uint8_t> data) {
    return send_all(to, data.data(), data.size());
  }
  [[nodiscard]] SendReceiveResult receive(SocketAddress& from, std::span<uint8_t> data) {
    return receive(from, data.data(), data.size());
  }
};

class SocketStream : public detail::RwSocket {
  friend class Listener;

  using RwSocket::RwSocket;

 public:
  SOCKLIB_NON_COPYABLE_NON_MOVABLE(SocketStream)

  struct ConnectResult {
    Status status{};
    std::unique_ptr<SocketStream> connection;

    constexpr operator bool() const { return status.success(); }
  };
  struct ConnectParameters {
    constexpr static ConnectParameters default_parameters() { return ConnectParameters{}; }
  };
  [[nodiscard]] static ConnectResult connect(
    const SocketAddress& address,
    const ConnectParameters& connect_parameters = ConnectParameters::default_parameters());
  [[nodiscard]] static ConnectResult connect(
    IpVersion ip_version,
    std::string_view hostname,
    uint16_t port,
    const ConnectParameters& connect_parameters = ConnectParameters::default_parameters());

  struct SendReceiveResult {
    Status status{};
    size_t byte_count{};

    constexpr operator bool() const { return status.success(); }
  };

  [[nodiscard]] SendReceiveResult send(const void* data, size_t data_size);
  [[nodiscard]] SendReceiveResult send_all(const void* data, size_t data_size);
  [[nodiscard]] SendReceiveResult receive(void* data, size_t data_size);

  [[nodiscard]] SendReceiveResult send(std::span<const uint8_t> data) {
    return send(data.data(), data.size());
  }
  [[nodiscard]] SendReceiveResult send_all(std::span<const uint8_t> data) {
    return send_all(data.data(), data.size());
  }
  [[nodiscard]] SendReceiveResult receive(std::span<uint8_t> data) {
    return receive(data.data(), data.size());
  }
};

class Listener : public Socket {
  using Socket::Socket;

 public:
  SOCKLIB_NON_COPYABLE_NON_MOVABLE(Listener)

  struct BindParameters {
    bool reuse_address = false;
    int max_pending_connections = 16;

    constexpr static BindParameters default_parameters() { return BindParameters{}; }
  };
  struct BindResult {
    Status status{};
    std::unique_ptr<Listener> listener;

    constexpr operator bool() const { return status.success(); }
  };
  [[nodiscard]] static BindResult bind(
    const SocketAddress& address,
    const BindParameters& bind_parameters = BindParameters::default_parameters());
  [[nodiscard]] static BindResult bind(
    IpVersion ip_version,
    std::string_view hostname,
    uint16_t port,
    const BindParameters& bind_parameters = BindParameters::default_parameters());

  struct AcceptResult {
    Status status{};
    std::unique_ptr<SocketStream> connection;

    constexpr operator bool() const { return status.success(); }
  };
  [[nodiscard]] AcceptResult accept(SocketAddress* remote_address = nullptr);
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

  struct PollResult {
    Status status{};
    size_t signaled_sockets{};

    constexpr operator bool() const { return status.success(); }
  };
  [[nodiscard]] virtual PollResult poll(std::span<PollEntry> entries, int timeout_ms) = 0;
};

SOCKLIB_IMPLEMENT_ENUM_BIT_OPERATIONS(Poller::QueryEvents)
SOCKLIB_IMPLEMENT_ENUM_BIT_OPERATIONS(Poller::StatusEvents)

[[nodiscard]] bool initialize();

}  // namespace sock

#undef SOCKLIB_NON_COPYABLE_NON_MOVABLE
#undef SOCKLIB_IMPLEMENT_ENUM_BIT_OPERATIONS