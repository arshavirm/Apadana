#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace apadana {

struct ServiceRecord {
		std::string name;
		std::string description;
		std::string load_state;
		std::string active_state;
		std::string sub_state;
		std::string unit_file_state;
};

struct ServiceActionResult {
		bool success{};
		std::string message;
};

class ServiceManager {
	public:
		[[nodiscard]] bool available() const;
		[[nodiscard]] std::string backend_name() const;
		[[nodiscard]] std::vector<ServiceRecord> list_services() const;

		ServiceActionResult start(const std::string& name) const;
		ServiceActionResult stop(const std::string& name) const;
		ServiceActionResult restart(const std::string& name) const;
		ServiceActionResult enable(const std::string& name) const;
		ServiceActionResult disable(const std::string& name) const;

		static bool valid_service_name(std::string_view name);

	private:
		ServiceActionResult run_action(std::string_view action, const std::string& name) const;
};

std::vector<ServiceRecord> parse_systemctl_show(std::string_view output);

} // namespace apadana
