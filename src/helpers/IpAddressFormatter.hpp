#pragma once
#include <socklib/Address.hpp>

class IpAddressFormatter {
 public:
  static std::string format(const sock::IpV6Address& address);
  static std::string format(const sock::IpV4Address& address);
};