#include <base/Initialization.hpp>
#include <base/Log.hpp>
#include <base/Panic.hpp>
#include <socklib/Socket.hpp>

#include <tools/receiver/Receiver.hpp>
#include <tools/sender/Sender.hpp>

#include <string_view>
#include <vector>

enum class Tool {
  Sender,
  Receiver,
  Invalid,
};

static Tool pick_tool(std::span<const std::string_view> args) {
  if (args.empty()) {
    return Tool::Invalid;
  }

  const auto tool_name = args[0];

  if (tool_name == "send" || tool_name == "upload") {
    return Tool::Sender;
  }
  if (tool_name == "receive" || tool_name == "recv" || tool_name == "download") {
    return Tool::Receiver;
  }

  return Tool::Invalid;
}

static bool run(std::span<const std::string_view> args) {
  const auto tool = pick_tool(args);
  if (tool != Tool::Invalid) {
    const auto tool_args = args.subspan(1);
    switch (tool) {
      case Tool::Sender:
        return tools::sender::run(tool_args);
      case Tool::Receiver:
        return tools::reciever::run(tool_args);
      default:
        unreachable();
    }
  } else {
    log_error("usage: ft [send/receive] [args...]");
    return false;
  }
}

int main(int argc, const char* argv[]) {
  base::initialize();

  std::vector<std::string_view> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  return run(args) ? EXIT_SUCCESS : EXIT_FAILURE;
}
