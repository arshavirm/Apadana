#include "apadana/platform.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>
#include <unordered_map>

namespace apadana {
namespace {

std::string trim(std::string value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

} // namespace

std::string unquote_os_release_value(std::string_view value) {
	std::string result = trim(std::string(value));
	if (result.size() >= 2 && ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\''))) {
		result = result.substr(1, result.size() - 2);
	}

	std::string decoded;
	decoded.reserve(result.size());
	bool escaped = false;
	for (const char character : result) {
		if (escaped) {
			decoded.push_back(character);
			escaped = false;
		} else if (character == '\\') {
			escaped = true;
		} else {
			decoded.push_back(character);
		}
	}
	if (escaped) {
		decoded.push_back('\\');
	}
	return decoded;
}

PlatformInfo parse_os_release(std::istream& input) {
	std::unordered_map<std::string, std::string> values;
	std::string line;
	while (std::getline(input, line)) {
		line = trim(line);
		if (line.empty() || line.front() == '#') {
			continue;
		}
		const auto separator = line.find('=');
		if (separator == std::string::npos) {
			continue;
		}
		const auto key = trim(line.substr(0, separator));
		values[key]    = unquote_os_release_value(line.substr(separator + 1));
	}

	PlatformInfo info;
	info.operating_system = "Linux";
	info.distribution     = values.find("PRETTY_NAME") != values.end() ? values["PRETTY_NAME"] : values["NAME"];
	info.distribution_id  = values["ID"];
	info.version	      = values.find("VERSION_ID") != values.end() ? values["VERSION_ID"] : values["VERSION"];
	return info;
}

bool executable_on_path(const std::string_view name, const std::string_view path) {
	if (name.empty() || name.find('/') != std::string_view::npos) {
		return false;
	}

	std::stringstream entries{std::string(path)};
	std::string directory;
	while (std::getline(entries, directory, ':')) {
		if (directory.empty()) {
			directory = ".";
		}
		const auto candidate = std::filesystem::path(directory) / name;
		if (::access(candidate.c_str(), X_OK) == 0) {
			return true;
		}
	}
	return false;
}

bool executable_on_path(const std::string_view name) {
	const char* path = std::getenv("PATH");
	return path != nullptr && executable_on_path(name, path);
}

PlatformInfo detect_platform() {
	PlatformInfo info;

	utsname system{};
	if (::uname(&system) == 0) {
		info.operating_system = system.sysname;
		info.kernel	      = std::string(system.release);
		info.architecture     = system.machine;
		info.hostname	      = system.nodename;
	}

	std::ifstream os_release("/etc/os-release");
	if (os_release) {
		auto release	     = parse_os_release(os_release);
		info.distribution    = std::move(release.distribution);
		info.distribution_id = std::move(release.distribution_id);
		info.version	     = std::move(release.version);
	}

	if (info.distribution.empty()) {
		info.distribution = info.operating_system.empty() ? "Unknown Unix" : info.operating_system;
	}
	return info;
}

} // namespace apadana
