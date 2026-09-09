#include "apadana/package_manager.hpp"

#include "apadana/command.hpp"
#include "apadana/platform.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace apadana {
namespace {

bool starts_with(const std::string& value, const std::string& prefix) {
	return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

std::string trim(std::string value) {
	const auto first = value.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) {
		return {};
	}
	const auto last = value.find_last_not_of(" \t\r\n");
	return value.substr(first, last - first + 1);
}

std::vector<std::string> split_tabs(const std::string& line) {
	std::vector<std::string> fields;
	std::stringstream stream(line);
	std::string field;
	while (std::getline(stream, field, '\t')) {
		fields.push_back(std::move(field));
	}
	return fields;
}

PackageActionResult package_action(const std::vector<std::string>& command) {
	const auto result = run_command_interactive(command, true);
	return {result.success, result.message};
}

} // namespace

std::vector<PackageManager> detect_package_managers() {
	static const std::array candidates{
	    PackageManager{"apt", "APT", "apt", PackageScope::System, true},
	    PackageManager{"dnf", "DNF", "dnf", PackageScope::System, true},
	    PackageManager{"yum", "YUM", "yum", PackageScope::System, true},
	    PackageManager{"pacman", "Pacman", "pacman", PackageScope::System, true},
	    PackageManager{"zypper", "Zypper", "zypper", PackageScope::System, true},
	    PackageManager{"apk", "APK", "apk", PackageScope::System, true},
	    PackageManager{"xbps", "XBPS", "xbps-install", PackageScope::System, true},
	    PackageManager{"emerge", "Portage", "emerge", PackageScope::System, true},
	    PackageManager{"nix", "Nix", "nix", PackageScope::Universal, false},
	    PackageManager{"flatpak", "Flatpak", "flatpak", PackageScope::Universal, false},
	    PackageManager{"snap", "Snap", "snap", PackageScope::Universal, true},
	    PackageManager{"brew", "Homebrew", "brew", PackageScope::User, false},
	    PackageManager{"pkg", "FreeBSD pkg", "pkg", PackageScope::System, true},
	};

	std::vector<PackageManager> detected;
	for (const auto& candidate : candidates) {
		if (executable_on_path(candidate.executable)) {
			detected.push_back(candidate);
		}
	}
	return detected;
}

std::string to_string(const PackageScope scope) {
	switch (scope) {
	case PackageScope::System:
		return "system";
	case PackageScope::User:
		return "user";
	case PackageScope::Universal:
		return "universal";
	}
	return "unknown";
}

std::vector<PackageRecord> parse_dpkg_query(const std::string_view output) {
	std::vector<PackageRecord> packages;
	std::stringstream lines{std::string(output)};
	std::string line;
	while (std::getline(lines, line)) {
		auto fields = split_tabs(line);
		if (fields.empty() || fields[0].empty()) {
			continue;
		}
		PackageRecord package;
		package.name = std::move(fields[0]);
		if (fields.size() > 1) {
			package.version = std::move(fields[1]);
		}
		if (fields.size() > 2) {
			package.description = std::move(fields[2]);
		}
		package.installed = true;
		packages.push_back(std::move(package));
	}
	return packages;
}

std::vector<PackageRecord> parse_apt_cache_search(const std::string_view output) {
	std::vector<PackageRecord> packages;
	std::stringstream lines{std::string(output)};
	std::string line;
	while (std::getline(lines, line)) {
		const auto separator = line.find(" - ");
		if (separator == std::string::npos || separator == 0) {
			continue;
		}
		PackageRecord package;
		package.name	    = trim(line.substr(0, separator));
		package.description = trim(line.substr(separator + 3));
		packages.push_back(std::move(package));
	}
	return packages;
}

std::vector<PackageRecord> parse_apt_upgradable(const std::string_view output) {
	std::vector<PackageRecord> packages;
	std::stringstream lines{std::string(output)};
	std::string line;
	while (std::getline(lines, line)) {
		if (line.empty() || starts_with(line, "Listing...") || starts_with(line, "WARNING:")) {
			continue;
		}
		const auto slash       = line.find('/');
		const auto first_space = line.find(' ');
		if (slash == std::string::npos || first_space == std::string::npos || slash > first_space) {
			continue;
		}
		PackageRecord package;
		package.name		= line.substr(0, slash);
		const auto second_space = line.find(' ', first_space + 1);
		package.version		= line.substr(first_space + 1, second_space - first_space - 1);
		const auto old_marker	= line.find("[upgradable from: ");
		if (old_marker != std::string::npos) {
			const auto old_end  = line.find(']', old_marker);
			package.description = "installed " + line.substr(old_marker + 18, old_end - old_marker - 18);
		}
		package.installed  = true;
		package.upgradable = true;
		packages.push_back(std::move(package));
	}
	return packages;
}

bool AptPackageManager::available() const {
	return executable_on_path("apt") && executable_on_path("apt-get") && executable_on_path("apt-cache") && executable_on_path("dpkg-query");
}

std::vector<PackageRecord> AptPackageManager::installed_packages() const {
	if (!available()) {
		return {};
	}
	const auto result = run_command_capture({"dpkg-query", "--show", "--showformat=${binary:Package}\\t${Version}\\t${binary:Summary}\\n"});
	return result.success ? parse_dpkg_query(result.output) : std::vector<PackageRecord>{};
}

std::vector<PackageRecord> AptPackageManager::search(const std::string_view query) const {
	if (!available() || query.empty() || query.size() > 100 ||
	    std::any_of(query.begin(), query.end(), [](const unsigned char value) { return std::iscntrl(value) != 0; })) {
		return {};
	}
	const auto result = run_command_capture({"apt-cache", "search", "--", std::string(query)});
	return result.success ? parse_apt_cache_search(result.output) : std::vector<PackageRecord>{};
}

std::vector<PackageRecord> AptPackageManager::upgradable_packages() const {
	if (!available()) {
		return {};
	}
	const auto result = run_command_capture({"apt", "list", "--upgradable"});
	return result.success ? parse_apt_upgradable(result.output) : std::vector<PackageRecord>{};
}

bool AptPackageManager::valid_package_name(const std::string_view name) {
	if (name.empty() || name.size() > 255 || !std::isalnum(static_cast<unsigned char>(name.front()))) {
		return false;
	}
	return std::all_of(name.begin(), name.end(), [](const unsigned char value) {
		return std::islower(value) != 0 || std::isdigit(value) != 0 || value == '+' || value == '-' || value == '.' || value == ':';
	});
}

PackageActionResult AptPackageManager::update_index() const {
	return available() ? package_action({"apt-get", "update"}) : PackageActionResult{false, "APT is not available"};
}

PackageActionResult AptPackageManager::install(const std::string& package) const {
	if (!valid_package_name(package)) {
		return {false, "Invalid APT package name"};
	}
	return available() ? package_action({"apt-get", "install", package}) : PackageActionResult{false, "APT is not available"};
}

PackageActionResult AptPackageManager::remove(const std::string& package) const {
	if (!valid_package_name(package)) {
		return {false, "Invalid APT package name"};
	}
	return available() ? package_action({"apt-get", "remove", package}) : PackageActionResult{false, "APT is not available"};
}

PackageActionResult AptPackageManager::upgrade_all() const {
	return available() ? package_action({"apt-get", "upgrade"}) : PackageActionResult{false, "APT is not available"};
}

} // namespace apadana
