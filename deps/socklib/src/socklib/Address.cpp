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
  const auto& c = components_;

  // TODO: Compress the address according to the rules.
  std::ostringstream os;
  os << std::hex << c[0] << ':' << c[1] << ':' << c[2] << ':' << c[3] << ':' << c[4] << ':' << c[5]
     << ':' << c[6] << ':' << c[7];
  return os.str();
}

std::string SocketIpV4Address::stringify() const {
  std::ostringstream os;
  os << ip_.stringify() << ':' << port_;
  return os.str();
}

std::string SocketIpV6Address::stringify() const {
  std::ostringstream os;
  os << '[' << ip_.stringify() << "]:" << port_;
  return os.str();
}

}  // namespace sock