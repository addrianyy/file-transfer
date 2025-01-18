#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sock {

enum class IpVersion {
  V4,
  V6,
};

class IpV4Address {
  std::array<uint8_t, 4> components_{};

 public:
  constexpr static IpVersion Version = IpVersion::V4;

  constexpr static IpV4Address unspecified() { return IpV4Address{{0, 0, 0, 0}}; }
  constexpr static IpV4Address loopback() { return IpV4Address{{127, 0, 0, 1}}; }
  constexpr static IpV4Address broadcast() { return IpV4Address{{255, 255, 255, 255}}; }

  constexpr explicit IpV4Address() = default;
  constexpr explicit IpV4Address(std::array<uint8_t, 4> components) : components_(components) {}
  constexpr explicit IpV4Address(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : components_({a, b, c, d}) {}

  constexpr std::array<uint8_t, 4> components() const { return components_; }

  std::string stringify() const;
};

class IpV6Address {
  std::array<uint16_t, 8> components_{};

 public:
  constexpr static IpVersion Version = IpVersion::V6;

  constexpr static IpV6Address unspecified() { return IpV6Address{{0, 0, 0, 0, 0, 0, 0}}; }
  constexpr static IpV6Address loopback() { return IpV6Address{{0, 0, 0, 0, 0, 0, 0, 1}}; }
  constexpr static IpV6Address mapped_to_ipv4(const IpV4Address& ipv4_address) {
    const auto components = ipv4_address.components();
    const auto part1 = (uint16_t(components[3]) << 8) | (uint16_t(components[2]) << 0);
    const auto part2 = (uint16_t(components[1]) << 8) | (uint16_t(components[0]) << 0);
    return IpV6Address{{0, 0, 0, 0, 0, 0xffff, uint16_t(part1), uint16_t(part2)}};
  }

  constexpr explicit IpV6Address() = default;
  constexpr explicit IpV6Address(std::array<uint16_t, 8> components) : components_(components) {}

  constexpr bool is_mapped_to_ipv4() const {
    const auto& c = components_;
    return c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 0 && c[4] == 0 && c[5] == 0xffff;
  }

  constexpr std::optional<IpV4Address> mapped_ipv4() const {
    if (!is_mapped_to_ipv4()) {
      return std::nullopt;
    }

    const auto part1 = components_[6];
    const auto part2 = components_[7];
    return IpV4Address{
      {uint8_t(part1 >> 8), uint8_t(part1 & 0xff), uint8_t(part2 >> 8), uint8_t(part2 & 0xff)},
    };
  }

  constexpr std::array<uint16_t, 8> components() const { return components_; }

  std::string stringify() const;
  std::string stringify_v6() const;
};

class SocketAddress {
 public:
  enum class Type {
    IpV4,
    IpV6,
    Unix,
  };

 protected:
  const Type type_;

  constexpr explicit SocketAddress(Type type) : type_(type) {}

 public:
  constexpr Type type() const { return type_; }
};

class SocketIpV4Address : public SocketAddress {
  IpV4Address ip_{};
  uint16_t port_{};

 public:
  using Ip = IpV4Address;
  constexpr static IpVersion Version = Ip::Version;

  constexpr SocketIpV4Address() : SocketAddress(Type::IpV4) {}
  constexpr SocketIpV4Address(const IpV4Address& ip, uint16_t port)
      : SocketAddress(Type::IpV4), ip_(ip), port_(port) {}
  constexpr SocketIpV4Address(const SocketIpV4Address& other)
      : SocketAddress(Type::IpV4), ip_(other.ip_), port_(other.port_) {}

  constexpr SocketIpV4Address& operator=(const SocketIpV4Address& other) {
    if (this != &other) {
      this->ip_ = other.ip_;
      this->port_ = other.port_;
    }
    return *this;
  }

  constexpr IpV4Address ip() const { return ip_; }
  constexpr uint16_t port() const { return port_; }

  std::string stringify() const;
};

class SocketIpV6Address : public SocketAddress {
  IpV6Address ip_{};
  uint16_t port_{};

 public:
  using Ip = IpV6Address;
  constexpr static IpVersion Version = Ip::Version;

  constexpr SocketIpV6Address() : SocketAddress(Type::IpV6) {}
  constexpr SocketIpV6Address(const IpV6Address& ip, uint16_t port)
      : SocketAddress(Type::IpV6), ip_(ip), port_(port) {}
  constexpr SocketIpV6Address(const SocketIpV6Address& other)
      : SocketAddress(Type::IpV6), ip_(other.ip_), port_(other.port_) {}

  constexpr SocketIpV6Address& operator=(const SocketIpV6Address& other) {
    if (this != &other) {
      this->ip_ = other.ip_;
      this->port_ = other.port_;
    }
    return *this;
  }

  constexpr IpV6Address ip() const { return ip_; }
  constexpr uint16_t port() const { return port_; }

  std::string stringify() const;
  std::string stringify_v6() const;
};

class SocketUnixAddress : public SocketAddress {
 public:
  enum class Namespace {
    Filesystem,
    Abstract,
  };

#if defined(__linux__)
  constexpr static bool abstract_namespace_supported = true;
#else
  constexpr static bool abstract_namespace_supported = false;
#endif

  // Minimum value of all supported OSes - 1 char (for abstract namespace or null terminator).
  constexpr static size_t max_path_size = 103;

 private:
  using PathBuffer = std::array<char, max_path_size>;

  Namespace socket_namespace_{};
  PathBuffer path_{};
  size_t path_size_{};

  SocketUnixAddress(Namespace socket_namespace, std::string_view path);

 public:
  constexpr SocketUnixAddress() : SocketAddress(Type::Unix) {}
  constexpr SocketUnixAddress(const SocketUnixAddress& other)
      : SocketAddress(Type::Unix),
        socket_namespace_(other.socket_namespace_),
        path_(other.path_),
        path_size_(other.path_size_) {}

  static std::optional<SocketUnixAddress> create(Namespace socket_namespace, std::string_view path);

  constexpr SocketUnixAddress& operator=(const SocketUnixAddress& other) {
    if (this != &other) {
      this->socket_namespace_ = other.socket_namespace_;
      this->path_ = other.path_;
      this->path_size_ = other.path_size_;
    }
    return *this;
  }

  constexpr Namespace socket_namespace() const { return socket_namespace_; }
  constexpr std::string_view path() const { return {path_.data(), path_size_}; }
};

}  // namespace sock