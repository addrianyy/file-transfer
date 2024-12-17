#include "IpAddressFormatter.hpp"

#include <base/text/Format.hpp>

std::string IpAddressFormatter::format(const sock::IpV6Address& address) {
  if (const auto ipv4 = address.extract_mapped_ipv4()) {
    return format(*ipv4);
  }

  const auto components = address.components();
  return base::format("{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}:{:x}", components[0], components[1],
                      components[2], components[3], components[4], components[5], components[6],
                      components[7]);
}

std::string IpAddressFormatter::format(const sock::IpV4Address& address) {
  const auto components = address.components();
  return base::format("{}.{}.{}.{}", components[0], components[1], components[2], components[3]);
}