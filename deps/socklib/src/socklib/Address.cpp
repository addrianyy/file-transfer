#include "Address.hpp"

#include <ios>
#include <sstream>

namespace sock {

std::string IpV4Address::stringify() const {
  const auto& c = components_;

  std::ostringstream os;
  os << uint32_t(c[0]) << '.' << uint32_t(c[1]) << '.' << uint32_t(c[2]) << '.' << uint32_t(c[3]);
  return os.str();
}

std::string IpV6Address::stringify() const {
  if (is_mapped_to_ipv4()) {
    return mapped_ipv4()->stringify();
  }
  return stringify_v6();
}

std::string IpV6Address::stringify_v6() const {
  size_t longest_sequence_begin = 0;
  size_t longest_sequence_size = 0;

  {
    size_t current_sequence_begin = 0;
    size_t current_sequence_size = 0;

    const auto flush = [&] {
      if (current_sequence_size >= 2 && current_sequence_size > longest_sequence_size) {
        longest_sequence_begin = current_sequence_begin;
        longest_sequence_size = current_sequence_size;
      }

      current_sequence_begin = 0;
      current_sequence_size = 0;
    };

    for (size_t i = 0; i < components_.size(); ++i) {
      if (components_[i] == 0) {
        if (current_sequence_size == 0) {
          current_sequence_begin = i;
        }
        current_sequence_size++;
      } else {
        flush();
      }
    }

    flush();
  }

  std::ostringstream os;

  os << std::hex;

  for (size_t i = 0; i < components_.size(); ++i) {
    if (i >= longest_sequence_begin && i < (longest_sequence_begin + longest_sequence_size)) {
      if (i == longest_sequence_begin) {
        os << ':';
        if (i == 0) {
          os << ':';
        }
      }
    } else {
      os << uint32_t(components_[i]);
      if (i + 1 != components_.size()) {
        os << ':';
      }
    }
  }

  return os.str();
}

std::string SocketIpV4Address::stringify() const {
  std::ostringstream os;
  os << ip_.stringify() << ':' << port_;
  return os.str();
}

std::string SocketIpV6Address::stringify() const {
  if (ip_.is_mapped_to_ipv4()) {
    return SocketIpV4Address{*ip_.mapped_ipv4(), port_}.stringify();
  }
  return stringify_v6();
}

std::string SocketIpV6Address::stringify_v6() const {
  std::ostringstream os;
  os << '[' << ip_.stringify() << "]:" << port_;
  return os.str();
}

}  // namespace sock