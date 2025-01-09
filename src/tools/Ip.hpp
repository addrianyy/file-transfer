#pragma once
#include <socklib/Address.hpp>

namespace tools {

using SocketIpAddress = sock::SocketIpV6Address;

using IpAddress = SocketIpAddress::Ip;

}  // namespace tools