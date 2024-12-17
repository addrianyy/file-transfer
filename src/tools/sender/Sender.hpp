#pragma once
#include <span>
#include <string_view>

namespace tools::sender {
bool run(std::span<const std::string_view> args);
}