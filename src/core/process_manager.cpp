#include "apadana/process_manager.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <sstream>
#include <unistd.h>

namespace apadana {
namespace {

bool starts_with(const std::string& value, const std::string& prefix) {
	return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string username_for(const uid_t uid) {
	if (const passwd* entry = ::getpwuid(uid); entry != nullptr && entry->pw_name != nullptr) {
		return entry->pw_name;
	}
	return std::to_string(uid);
}

std::string trim(std::string value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

} // namespace

bool parse_proc_stat(const std::string_view line, char& state, std::uint64_t& cpu_ticks, std::int64_t& resident_pages) {
	const auto close = line.rfind(')');
	if (close == std::string_view::npos || close + 3 >= line.size()) {
		return false;
	}

	std::istringstream fields(std::string(line.substr(close + 2)));
	std::vector<std::string> values;
	std::string value;
	while (fields >> value) {
		values.push_back(std::move(value));
	}
	// Values begin at proc stat field 3 (state); utime=14, stime=15, rss=24.
	if (values.size() < 22 || values[0].empty()) {
		return false;
	}
	try {
		state	       = values[0].front();
		cpu_ticks      = std::stoull(values[11]) + std::stoull(values[12]);
		resident_pages = std::stoll(values[21]);
		return true;
	} catch (...) {
		return false;
	}
}

std::string read_process_command(std::istream& command_line, const std::string_view fallback) {
	std::string command((std::istreambuf_iterator<char>(command_line)), std::istreambuf_iterator<char>());
	for (char& character : command) {
		// Control characters (newlines, tabs, NUL) would corrupt single-line rendering.
		if (static_cast<unsigned char>(character) < 0x20 || static_cast<unsigned char>(character) == 0x7f) {
			character = ' ';
		}
	}
	command = trim(std::move(command));
	return command.empty() ? std::string(fallback) : command;
}

std::string process_state_name(const char state) {
	switch (state) {
	case 'R':
		return "running";
	case 'S':
		return "sleeping";
	case 'D':
		return "disk wait";
	case 'T':
	case 't':
		return "stopped";
	case 'Z':
		return "zombie";
	case 'I':
		return "idle";
	default:
		return "unknown";
	}
}

bool ProcessManager::available() const { return std::filesystem::is_directory("/proc"); }

std::vector<ProcessRecord> ProcessManager::list_processes() const {
	std::vector<ProcessRecord> processes;
	if (!available()) {
		return processes;
	}

	std::ifstream uptime_input("/proc/uptime");
	double uptime = 0.0;
	uptime_input >> uptime;
	const long ticks_per_second = ::sysconf(_SC_CLK_TCK);
	const long page_size	    = ::sysconf(_SC_PAGESIZE);

	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator("/proc", std::filesystem::directory_options::skip_permission_denied, error)) {
		if (error || !entry.is_directory(error)) {
			error.clear();
			continue;
		}
		const std::string filename = entry.path().filename().string();
		if (filename.empty() || !std::all_of(filename.begin(), filename.end(), [](const unsigned char character) { return character >= '0' && character <= '9'; })) {
			continue;
		}

		ProcessRecord process;
		try {
			process.pid = std::stoi(filename);
		} catch (...) {
			continue;
		}

		std::ifstream stat_input(entry.path() / "stat");
		std::string stat_line;
		std::getline(stat_input, stat_line);
		std::uint64_t cpu_ticks	    = 0;
		std::int64_t resident_pages = 0;
		if (!parse_proc_stat(stat_line, process.state, cpu_ticks, resident_pages)) {
			continue;
		}
		if (uptime > 0.0 && ticks_per_second > 0) {
			process.cpu_percent = (static_cast<double>(cpu_ticks) / static_cast<double>(ticks_per_second)) / uptime * 100.0;
		}
		if (resident_pages > 0 && page_size > 0) {
			process.resident_bytes = static_cast<std::uint64_t>(resident_pages) * static_cast<std::uint64_t>(page_size);
		}

		std::ifstream status_input(entry.path() / "status");
		std::string status_line;
		while (std::getline(status_input, status_line)) {
			if (starts_with(status_line, "Uid:")) {
				std::istringstream uid_fields(status_line.substr(4));
				unsigned long uid = 0;
				uid_fields >> uid;
				process.user = username_for(static_cast<uid_t>(uid));
				break;
			}
		}

		std::ifstream comm_input(entry.path() / "comm");
		std::string fallback;
		std::getline(comm_input, fallback);
		std::ifstream command_input(entry.path() / "cmdline", std::ios::binary);
		process.command = read_process_command(command_input, fallback);
		processes.push_back(std::move(process));
	}

	std::sort(processes.begin(), processes.end(), [](const ProcessRecord& left, const ProcessRecord& right) {
		if (left.cpu_percent != right.cpu_percent) {
			return left.cpu_percent > right.cpu_percent;
		}
		return left.pid < right.pid;
	});
	return processes;
}

ProcessActionResult ProcessManager::terminate(const int pid, const bool force) const {
	if (pid <= 1 || pid == static_cast<int>(::getpid())) {
		return {false, "Refusing to signal this protected process"};
	}
	const int signal = force ? SIGKILL : SIGTERM;
	if (::kill(static_cast<pid_t>(pid), signal) == 0) {
		return {true, force ? "Process killed" : "Termination signal sent"};
	}
	return {false, std::string("Could not signal process: ") + std::strerror(errno)};
}

} // namespace apadana
