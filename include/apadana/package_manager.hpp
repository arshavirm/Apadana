#pragma once

#include <memory>
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

// Interface implemented by every package ecosystem backend. Inspection is
// unprivileged; mutations validate their input and elevate when required.
class PackageManagerBackend {
	public:
		virtual ~PackageManagerBackend() = default;
		[[nodiscard]] virtual std::string backend_name() const = 0;
		[[nodiscard]] virtual bool available() const = 0;
		[[nodiscard]] virtual std::vector<PackageRecord> installed_packages() const = 0;
		[[nodiscard]] virtual std::vector<PackageRecord> search(std::string_view query) const = 0;
		[[nodiscard]] virtual std::vector<PackageRecord> upgradable_packages() const = 0;
		[[nodiscard]] virtual PackageActionResult update_index() const = 0;
		[[nodiscard]] virtual PackageActionResult install(const std::string& package) const = 0;
		[[nodiscard]] virtual PackageActionResult remove(const std::string& package) const = 0;
		[[nodiscard]] virtual PackageActionResult upgrade_all() const = 0;
};

class AptPackageManager final : public PackageManagerBackend {
	public:
		[[nodiscard]] std::string backend_name() const override { return "APT"; }
		[[nodiscard]] bool available() const override;
		[[nodiscard]] std::vector<PackageRecord> installed_packages() const override;
		[[nodiscard]] std::vector<PackageRecord> search(std::string_view query) const override;
		[[nodiscard]] std::vector<PackageRecord> upgradable_packages() const override;

		[[nodiscard]] PackageActionResult update_index() const override;
		[[nodiscard]] PackageActionResult install(const std::string& package) const override;
		[[nodiscard]] PackageActionResult remove(const std::string& package) const override;
		[[nodiscard]] PackageActionResult upgrade_all() const override;

		static bool valid_package_name(std::string_view name);
};

class DnfPackageManager final : public PackageManagerBackend {
	public:
		[[nodiscard]] std::string backend_name() const override { return "DNF"; }
		[[nodiscard]] bool available() const override;
		[[nodiscard]] std::vector<PackageRecord> installed_packages() const override;
		[[nodiscard]] std::vector<PackageRecord> search(std::string_view query) const override;
		[[nodiscard]] std::vector<PackageRecord> upgradable_packages() const override;

		[[nodiscard]] PackageActionResult update_index() const override;
		[[nodiscard]] PackageActionResult install(const std::string& package) const override;
		[[nodiscard]] PackageActionResult remove(const std::string& package) const override;
		[[nodiscard]] PackageActionResult upgrade_all() const override;

		static bool valid_package_name(std::string_view name);
};

class PacmanPackageManager final : public PackageManagerBackend {
	public:
		[[nodiscard]] std::string backend_name() const override { return "Pacman"; }
		[[nodiscard]] bool available() const override;
		[[nodiscard]] std::vector<PackageRecord> installed_packages() const override;
		[[nodiscard]] std::vector<PackageRecord> search(std::string_view query) const override;
		[[nodiscard]] std::vector<PackageRecord> upgradable_packages() const override;

		[[nodiscard]] PackageActionResult update_index() const override;
		[[nodiscard]] PackageActionResult install(const std::string& package) const override;
		[[nodiscard]] PackageActionResult remove(const std::string& package) const override;
		[[nodiscard]] PackageActionResult upgrade_all() const override;

		static bool valid_package_name(std::string_view name);
};

// Returns the first supported backend available on this host, or null.
[[nodiscard]] std::unique_ptr<PackageManagerBackend> create_package_backend();

std::vector<PackageManager> detect_package_managers();
std::string to_string(PackageScope scope);
std::vector<PackageRecord> parse_dpkg_query(std::string_view output);
std::vector<PackageRecord> parse_apt_cache_search(std::string_view output);
std::vector<PackageRecord> parse_apt_upgradable(std::string_view output);
std::vector<PackageRecord> parse_rpm_qa(std::string_view output);
std::vector<PackageRecord> parse_dnf_search(std::string_view output);
std::vector<PackageRecord> parse_dnf_check_update(std::string_view output);
std::vector<PackageRecord> parse_pacman_query(std::string_view output);
std::vector<PackageRecord> parse_pacman_search(std::string_view output);
std::vector<PackageRecord> parse_pacman_upgradable(std::string_view output);

} // namespace apadana
