#include "apadana/command.hpp"

#include "apadana/platform.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace apadana {
namespace {

std::vector<char*> make_argv(std::vector<std::string>& command) {
	std::vector<char*> argv;
	argv.reserve(command.size() + 1);
	for (auto& argument : command) {
		argv.push_back(argument.data());
	}
	argv.push_back(nullptr);
	return argv;
}

CommandResult wait_for_child(const pid_t child) {
	int status = 0;
	while (::waitpid(child, &status, 0) < 0) {
		if (errno != EINTR) {
			return {false, -1, {}, std::string("Could not wait for command: ") + std::strerror(errno)};
		}
	}
	if (WIFEXITED(status)) {
		const int code = WEXITSTATUS(status);
		return {code == 0, code, {}, code == 0 ? "Command completed successfully" : "Command exited with status " + std::to_string(code)};
	}
	if (WIFSIGNALED(status)) {
		return {false, -1, {}, "Command terminated by signal " + std::to_string(WTERMSIG(status))};
	}
	return {false, -1, {}, "Command did not complete normally"};
}

} // namespace

CommandResult run_command_capture(const std::vector<std::string>& arguments) {
	if (arguments.empty()) {
		return {false, -1, {}, "No command was provided"};
	}

	std::array<int, 2> output_pipe{};
	if (::pipe(output_pipe.data()) != 0) {
		return {false, -1, {}, std::string("Could not create output pipe: ") + std::strerror(errno)};
	}

	std::vector<std::string> command = arguments;
	auto argv			 = make_argv(command);
	const pid_t child		 = ::fork();
	if (child < 0) {
		::close(output_pipe[0]);
		::close(output_pipe[1]);
		return {false, -1, {}, std::string("Could not start command: ") + std::strerror(errno)};
	}
	if (child == 0) {
		::close(output_pipe[0]);
		::dup2(output_pipe[1], STDOUT_FILENO);
		::dup2(output_pipe[1], STDERR_FILENO);
		::close(output_pipe[1]);
		::execvp(argv.front(), argv.data());
		_exit(127);
	}

	::close(output_pipe[1]);
	std::string output;
	std::array<char, 8192> buffer{};
	while (true) {
		const ssize_t count = ::read(output_pipe[0], buffer.data(), buffer.size());
		if (count > 0) {
			output.append(buffer.data(), static_cast<std::size_t>(count));
		} else if (count == 0) {
			break;
		} else if (errno != EINTR) {
			break;
		}
	}
	::close(output_pipe[0]);

	auto result   = wait_for_child(child);
	result.output = std::move(output);
	return result;
}

CommandResult run_command_interactive(const std::vector<std::string>& arguments, const bool elevate) {
	if (arguments.empty()) {
		return {false, -1, {}, "No command was provided"};
	}

	std::vector<std::string> command;
	if (elevate && ::geteuid() != 0) {
		if (executable_on_path("sudo")) {
			command = {"sudo", "--"};
		} else if (executable_on_path("pkexec")) {
			command = {"pkexec"};
		} else {
			return {false, -1, {}, "Run Apadana as root or install sudo/pkexec"};
		}
	}
	command.insert(command.end(), arguments.begin(), arguments.end());
	auto argv = make_argv(command);

	const pid_t child = ::fork();
	if (child < 0) {
		return {false, -1, {}, std::string("Could not start command: ") + std::strerror(errno)};
	}
	if (child == 0) {
		::execvp(argv.front(), argv.data());
		_exit(127);
	}
	return wait_for_child(child);
}

} // namespace apadana
