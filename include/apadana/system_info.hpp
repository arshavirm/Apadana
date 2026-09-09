#pragma once

#include "apadana/package_manager.hpp"
#include "apadana/platform.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace apadana {

struct MemoryInfo {
		std::uint64_t total_bytes{};
		std::uint64_t available_bytes{};
};

struct DiskInfo {
		std::uint64_t capacity_bytes{};
		std::uint64_t available_bytes{};
};

struct NetworkInfo {
		std::size_t interface_count{};
		std::size_t active_interface_count{};
		std::uint64_t received_bytes{};
		std::uint64_t transmitted_bytes{};
};

struct SecurityInfo {
		bool address_space_randomization{};
		bool firewall_tool_available{};
};

struct SystemSnapshot {
		PlatformInfo platform;
		MemoryInfo memory;
		DiskInfo root_disk;
		NetworkInfo network;
		SecurityInfo security;
		double load_average{};
		std::uint64_t uptime_seconds{};
		unsigned int logical_cpu_count{};
		std::size_t installed_package_count{};
		std::vector<PackageManager> package_managers;
};

SystemSnapshot collect_system_snapshot();
std::string format_bytes(std::uint64_t bytes);
std::string format_duration(std::uint64_t seconds);

} // namespace apadana
