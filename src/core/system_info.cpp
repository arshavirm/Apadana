#include "apadana/system_info.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

#include "apadana/platform.hpp"

namespace apadana {
namespace {

MemoryInfo read_memory_info() {
	std::ifstream input("/proc/meminfo");
	MemoryInfo memory;
	std::string key;
	std::uint64_t value_kib = 0;
	std::string unit;
	while (input >> key >> value_kib >> unit) {
		if (key == "MemTotal:") {
			memory.total_bytes = value_kib * 1024;
		} else if (key == "MemAvailable:") {
			memory.available_bytes = value_kib * 1024;
		}
	}
	return memory;
}

double read_load_average() {
	std::ifstream input("/proc/loadavg");
	double load = 0.0;
	input >> load;
	return load;
}

std::uint64_t read_uptime() {
	std::ifstream input("/proc/uptime");
	double seconds = 0.0;
	input >> seconds;
	return seconds > 0.0 ? static_cast<std::uint64_t>(seconds) : 0;
}

NetworkInfo read_network_info() {
	NetworkInfo network;
	std::ifstream input("/proc/net/dev");
	std::string line;
	while (std::getline(input, line)) {
		const auto colon = line.find(':');
		if (colon == std::string::npos) {
			continue;
		}
		const std::string raw_name = line.substr(0, colon);
		const auto first = raw_name.find_first_not_of(" \t");
		const auto last = raw_name.find_last_not_of(" \t");
		if (first == std::string::npos) {
			continue;
		}
		const std::string name = raw_name.substr(first, last - first + 1);
		if (name == "lo") continue;
		std::istringstream fields(line.substr(colon + 1));
		std::uint64_t rx = 0, tx = 0;
		if (fields >> rx) {
			std::string discard;
			for (int i = 0; i < 7; ++i) fields >> discard;
			fields >> tx;
		}
		++network.interface_count;
		network.received_bytes += rx;
		network.transmitted_bytes += tx;
		std::ifstream carrier("/sys/class/net/" + name + "/carrier");
		int up = 0;
		if (carrier >> up && up != 0) ++network.active_interface_count;
	}
	return network;
}

SecurityInfo read_security_info() {
	SecurityInfo security;
	std::ifstream aslr("/proc/sys/kernel/randomize_va_space");
	int aslr_mode = 0;
	security.address_space_randomization = static_cast<bool>(aslr >> aslr_mode) && aslr_mode > 0;
	security.firewall_tool_available = executable_on_path("nft") || executable_on_path("ufw") || executable_on_path("firewall-cmd");
	return security;
}

} // namespace

SystemSnapshot collect_system_snapshot() {
	SystemSnapshot snapshot;
	snapshot.platform	   = detect_platform();
	snapshot.memory		   = read_memory_info();
	snapshot.load_average	   = read_load_average();
	snapshot.uptime_seconds	   = read_uptime();
	snapshot.logical_cpu_count = std::thread::hardware_concurrency();
	snapshot.package_managers  = detect_package_managers();
	snapshot.network           = read_network_info();
	snapshot.security          = read_security_info();
	const AptPackageManager apt;
	if (apt.available()) {
		snapshot.installed_package_count = apt.installed_packages().size();
	}

	std::error_code error;
	const auto space = std::filesystem::space("/", error);
	if (!error) {
		snapshot.root_disk.capacity_bytes  = space.capacity;
		snapshot.root_disk.available_bytes = space.available;
	}
	return snapshot;
}

std::string format_bytes(const std::uint64_t bytes) {
	static constexpr std::array units{"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
	double amount	 = static_cast<double>(bytes);
	std::size_t unit = 0;
	while (amount >= 1024.0 && unit + 1 < units.size()) {
		amount /= 1024.0;
		++unit;
	}

	std::ostringstream output;
	output << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << amount << ' ' << units[unit];
	return output.str();
}

std::string format_duration(std::uint64_t seconds) {
	const auto days = seconds / 86400;
	seconds %= 86400;
	const auto hours = seconds / 3600;
	seconds %= 3600;
	const auto minutes = seconds / 60;

	std::ostringstream output;
	if (days > 0) {
		output << days << "d ";
	}
	if (hours > 0 || days > 0) {
		output << hours << "h ";
	}
	output << minutes << "m";
	return output.str();
}

} // namespace apadana
