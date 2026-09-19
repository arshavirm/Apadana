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

// Validation rules shared by the RPM and Pacman ecosystems.
bool valid_package_name_common(const std::string_view name) {
	if (name.empty() || name.size() > 255 || !std::isalnum(static_cast<unsigned char>(name.front()))) {
		return false;
	}
	return std::all_of(name.begin(), name.end(), [](const unsigned char value) {
		return std::islower(value) != 0 || std::isdigit(value) != 0 || value == '+' || value == '-' || value == '.' || value == '_' ||
		       value == ':';
	});
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

// -------------------------------------------------------------- DNF/YUM ---

bool DnfPackageManager::available() const {
	return executable_on_path("dnf") && executable_on_path("rpm");
}

std::vector<PackageRecord> parse_rpm_qa(const std::string_view output) {
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

std::vector<PackageRecord> DnfPackageManager::installed_packages() const {
	if (!available()) {
		return {};
	}
	const auto result = run_command_capture({"rpm", "-qa", "--qf", "%{NAME}\\t%{VERSION}-%{RELEASE}\\t%{SUMMARY}\\n"});
	return result.success ? parse_rpm_qa(result.output) : std::vector<PackageRecord>{};
}

std::vector<PackageRecord> parse_dnf_search(const std::string_view output) {
	std::vector<PackageRecord> packages;
	std::stringstream lines{std::string(output)};
	std::string line;
	while (std::getline(lines, line)) {
		// Matches "name : summary" entries; skips section separators and notices.
		const auto separator = line.find(" : ");
		if (separator == std::string::npos || separator == 0 || line.find('=') != std::string::npos) {
			continue;
		}
		PackageRecord package;
		package.name	    = trim(line.substr(0, separator));
		package.description = trim(line.substr(separator + 3));
		if (package.name.find(' ') != std::string::npos) {
			continue;
		}
		packages.push_back(std::move(package));
	}
	return packages;
}

std::vector<PackageRecord> DnfPackageManager::search(const std::string_view query) const {
	if (!available() || query.empty() || query.size() > 100 ||
	    std::any_of(query.begin(), query.end(), [](const unsigned char value) { return std::iscntrl(value) != 0; })) {
		return {};
	}
	const auto result = run_command_capture({"dnf", "search", "--", std::string(query)});
	return parse_dnf_search(result.output);
}

std::vector<PackageRecord> parse_dnf_check_update(const std::string_view output) {
	std::vector<PackageRecord> packages;
	std::stringstream lines{std::string(output)};
	std::string line;
	while (std::getline(lines, line)) {
		// Update lines look like "name.version<space-or-tab>version repo"; skip headers/notices.
		const auto first_space = line.find_first_of(" \t");
		if (line.empty() || first_space == std::string::npos) {
			continue;
		}
		const auto full_name = line.substr(0, first_space);
		const auto dot	     = full_name.rfind('.');
		if (dot == std::string::npos) {
			continue;
		}
		const auto arch = full_name.substr(dot + 1);
		if (arch != "x86_64" && arch != "i686" && arch != "aarch64" && arch != "noarch" && arch != "s390x" && arch != "ppc64le" &&
		    arch != "riscv64") {
			continue;
		}
		PackageRecord package;
		package.name = full_name.substr(0, dot);
		auto fields = split_tabs(line);
		if (fields.size() > 1) {
			package.version = trim(fields[1]);
		}
		package.installed  = true;
		package.upgradable = true;
		packages.push_back(std::move(package));
	}
	return packages;
}

std::vector<PackageRecord> DnfPackageManager::upgradable_packages() const {
	if (!available()) {
		return {};
	}
	// dnf check-update exits 100 when updates exist, so inspect the output
	// regardless of the exit status.
	const auto result = run_command_capture({"dnf", "check-update", "--quiet"});
	return parse_dnf_check_update(result.output);
}

bool DnfPackageManager::valid_package_name(const std::string_view name) {
	return valid_package_name_common(name);
}

PackageActionResult DnfPackageManager::update_index() const {
	return available() ? package_action({"dnf", "makecache", "--quiet"}) : PackageActionResult{false, "DNF is not available"};
}

PackageActionResult DnfPackageManager::install(const std::string& package) const {
	if (!valid_package_name(package)) {
		return {false, "Invalid DNF package name"};
	}
	return available() ? package_action({"dnf", "install", "-y", "--", package}) : PackageActionResult{false, "DNF is not available"};
}

PackageActionResult DnfPackageManager::remove(const std::string& package) const {
	if (!valid_package_name(package)) {
		return {false, "Invalid DNF package name"};
	}
	return available() ? package_action({"dnf", "remove", "-y", "--", package}) : PackageActionResult{false, "DNF is not available"};
}

PackageActionResult DnfPackageManager::upgrade_all() const {
	return available() ? package_action({"dnf", "upgrade", "-y"}) : PackageActionResult{false, "DNF is not available"};
}

// --------------------------------------------------------------- pacman ---

bool PacmanPackageManager::available() const {
	return executable_on_path("pacman");
}

std::vector<PackageRecord> parse_pacman_query(const std::string_view output) {
	std::vector<PackageRecord> packages;
	std::stringstream lines{std::string(output)};
	std::string line;
	while (std::getline(lines, line)) {
		const auto space = line.find(' ');
		if (space == std::string::npos || space == 0) {
			continue;
		}
		PackageRecord package;
		package.name	   = line.substr(0, space);
		package.version    = trim(line.substr(space + 1));
		package.installed  = true;
		packages.push_back(std::move(package));
	}
	return packages;
}

std::vector<PackageRecord> PacmanPackageManager::installed_packages() const {
	if (!available()) {
		return {};
	}
	const auto result = run_command_capture({"pacman", "-Q"});
	return result.success ? parse_pacman_query(result.output) : std::vector<PackageRecord>{};
}

std::vector<PackageRecord> parse_pacman_search(const std::string_view output) {
	// pacman -Ss prints a "repo/name version" header line followed by an
	// indented description line for each result.
	std::vector<PackageRecord> packages;
	std::stringstream lines{std::string(output)};
	std::string line;
	while (std::getline(lines, line)) {
		if (line.empty()) {
			continue;
		}
		if (line.front() == ' ') {
			if (!packages.empty()) {
				packages.back().description = trim(line);
			}
			continue;
		}
		const auto slash = line.find('/');
		if (slash == std::string::npos || slash == 0) {
			continue;
		}
		const auto content = line.substr(slash + 1);
		const auto space   = content.find(' ');
		if (space == std::string::npos || space == 0) {
			continue;
		}
		PackageRecord package;
		package.name = content.substr(0, space);
		packages.push_back(std::move(package));
	}
	return packages;
}

std::vector<PackageRecord> PacmanPackageManager::search(const std::string_view query) const {
	if (!available() || query.empty() || query.size() > 100 ||
	    std::any_of(query.begin(), query.end(), [](const unsigned char value) { return std::iscntrl(value) != 0; })) {
		return {};
	}
	const auto result = run_command_capture({"pacman", "-Ss", "--", std::string(query)});
	return result.success ? parse_pacman_search(result.output) : std::vector<PackageRecord>{};
}

std::vector<PackageRecord> parse_pacman_upgradable(const std::string_view output) {
	std::vector<PackageRecord> packages;
	std::stringstream lines{std::string(output)};
	std::string line;
	while (std::getline(lines, line)) {
		const auto space = line.find(' ');
		if (space == std::string::npos || space == 0) {
			continue;
		}
		PackageRecord package;
		package.name	    = line.substr(0, space);
		package.description = "installed " + trim(line.substr(space + 1));
		package.installed   = true;
		package.upgradable  = true;
		packages.push_back(std::move(package));
	}
	return packages;
}

std::vector<PackageRecord> PacmanPackageManager::upgradable_packages() const {
	if (!available()) {
		return {};
	}
	// checkupdates (pacman-contrib) avoids touching the sync database; the
	// fallback works from the last refresh and formats "name old -> new".
	if (executable_on_path("checkupdates")) {
		const auto result = run_command_capture({"checkupdates"});
		if (result.success) {
			return parse_pacman_upgradable(result.output);
		}
	}
	const auto result = run_command_capture({"pacman", "-Qu"});
	return result.success ? parse_pacman_upgradable(result.output) : std::vector<PackageRecord>{};
}

bool PacmanPackageManager::valid_package_name(const std::string_view name) {
	return valid_package_name_common(name);
}

PackageActionResult PacmanPackageManager::update_index() const {
	return available() ? package_action({"pacman", "-Sy"}) : PackageActionResult{false, "Pacman is not available"};
}

PackageActionResult PacmanPackageManager::install(const std::string& package) const {
	if (!valid_package_name(package)) {
		return {false, "Invalid pacman package name"};
	}
	return available() ? package_action({"pacman", "-S", "--noconfirm", "--", package})
			   : PackageActionResult{false, "Pacman is not available"};
}

PackageActionResult PacmanPackageManager::remove(const std::string& package) const {
	if (!valid_package_name(package)) {
		return {false, "Invalid pacman package name"};
	}
	return available() ? package_action({"pacman", "-R", "--noconfirm", "--", package})
			   : PackageActionResult{false, "Pacman is not available"};
}

PackageActionResult PacmanPackageManager::upgrade_all() const {
	return available() ? package_action({"pacman", "-Syu", "--noconfirm"}) : PackageActionResult{false, "Pacman is not available"};
}

// -------------------------------------------------------------- factory ---

std::unique_ptr<PackageManagerBackend> create_package_backend() {
	if (AptPackageManager{}.available()) {
		return std::make_unique<AptPackageManager>();
	}
	if (DnfPackageManager{}.available()) {
		return std::make_unique<DnfPackageManager>();
	}
	if (PacmanPackageManager{}.available()) {
		return std::make_unique<PacmanPackageManager>();
	}
	return nullptr;
}

} // namespace apadana
