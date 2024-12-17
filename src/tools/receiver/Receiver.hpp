#pragma once
#include <span>
#include <string_view>

namespace tools::reciever {
bool run(std::span<const std::string_view> args);
}