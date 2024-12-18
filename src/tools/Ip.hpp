#pragma once
#include <socklib/Address.hpp>

namespace tools {

using SocketIpAddress = sock::SocketIpV4Address;
// using SocketIpAddress = sock::SocketIpV6Address;

using IpAddress = SocketIpAddress::Ip;

}