#pragma once

#include <string>
#include <vector>

namespace apadana {

struct CommandResult {
		bool success{};
		int exit_code{-1};
		std::string output;
		std::string message;
};

CommandResult run_command_capture(const std::vector<std::string>& arguments);
CommandResult run_command_interactive(const std::vector<std::string>& arguments, bool elevate);

} // namespace apadana
