#pragma once

#include <filesystem>
#include <istream>
#include <string>
#include <string_view>

namespace apadana {

struct PlatformInfo {
		std::string operating_system;
		std::string distribution;
		std::string distribution_id;
		std::string version;
		std::string kernel;
		std::string architecture;
		std::string hostname;
};

PlatformInfo detect_platform();
PlatformInfo parse_os_release(std::istream& input);
std::string unquote_os_release_value(std::string_view value);
bool executable_on_path(std::string_view name);
bool executable_on_path(std::string_view name, std::string_view path);

} // namespace apadana
