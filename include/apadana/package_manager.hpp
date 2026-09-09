#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace apadana {

enum class PackageScope {
	System,
	User,
	Universal,
};

struct PackageManager {
		std::string id;
		std::string display_name;
		std::string executable;
		PackageScope scope;
		bool requires_privilege;
};

struct PackageRecord {
		std::string name;
		std::string version;
		std::string description;
		bool installed{};
		bool upgradable{};
};

struct PackageActionResult {
		bool success{};
		std::string message;
};

class AptPackageManager {
	public:
		[[nodiscard]] bool available() const;
		[[nodiscard]] std::vector<PackageRecord> installed_packages() const;
		[[nodiscard]] std::vector<PackageRecord> search(std::string_view query) const;
		[[nodiscard]] std::vector<PackageRecord> upgradable_packages() const;

		PackageActionResult update_index() const;
		PackageActionResult install(const std::string& package) const;
		PackageActionResult remove(const std::string& package) const;
		PackageActionResult upgrade_all() const;

		static bool valid_package_name(std::string_view name);
};

std::vector<PackageManager> detect_package_managers();
std::string to_string(PackageScope scope);
std::vector<PackageRecord> parse_dpkg_query(std::string_view output);
std::vector<PackageRecord> parse_apt_cache_search(std::string_view output);
std::vector<PackageRecord> parse_apt_upgradable(std::string_view output);

} // namespace apadana
