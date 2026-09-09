#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace apadana {

struct ProcessRecord {
		int pid{};
		std::string user;
		char state{'?'};
		double cpu_percent{};
		std::uint64_t resident_bytes{};
		std::string command;
};

struct ProcessActionResult {
		bool success{};
		std::string message;
};

class ProcessManager {
	public:
		[[nodiscard]] bool available() const;
		[[nodiscard]] std::vector<ProcessRecord> list_processes() const;
		ProcessActionResult terminate(int pid, bool force = false) const;
};

bool parse_proc_stat(std::string_view line, char& state, std::uint64_t& cpu_ticks, std::int64_t& resident_pages);
std::string read_process_command(std::istream& command_line, std::string_view fallback);
std::string process_state_name(char state);

} // namespace apadana
