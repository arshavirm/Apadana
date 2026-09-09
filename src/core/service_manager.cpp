#include "apadana/service_manager.hpp"

#include "apadana/command.hpp"
#include "apadana/platform.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace apadana {
namespace {

bool ends_with(const std::string& value, const std::string& suffix) {
	return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void assign_property(ServiceRecord& service, const std::string_view key, const std::string_view value) {
	if (key == "Id")
		service.name = value;
	else if (key == "Description")
		service.description = value;
	else if (key == "LoadState")
		service.load_state = value;
	else if (key == "ActiveState")
		service.active_state = value;
	else if (key == "SubState")
		service.sub_state = value;
	else if (key == "UnitFileState")
		service.unit_file_state = value;
}

} // namespace

std::vector<ServiceRecord> parse_systemctl_show(const std::string_view output) {
	std::vector<ServiceRecord> services;
	ServiceRecord current;
	std::istringstream lines{std::string(output)};
	std::string line;
	auto finish = [&]() {
		if (!current.name.empty() && ends_with(current.name, ".service")) {
			services.push_back(std::move(current));
		}
		current = {};
	};
	while (std::getline(lines, line)) {
		if (line.empty()) {
			finish();
			continue;
		}
		const auto separator = line.find('=');
		if (separator != std::string::npos) {
			assign_property(current, std::string_view(line).substr(0, separator), std::string_view(line).substr(separator + 1));
		}
	}
	finish();
	std::sort(services.begin(), services.end(), [](const ServiceRecord& left, const ServiceRecord& right) { return left.name < right.name; });
	return services;
}

bool ServiceManager::available() const { return executable_on_path("systemctl"); }

std::string ServiceManager::backend_name() const { return available() ? "systemd" : "unavailable"; }

std::vector<ServiceRecord> ServiceManager::list_services() const {
	if (!available()) {
		return {};
	}
	const auto result =
	    run_command_capture({"systemctl", "show", "--type=service", "--all", "--property=Id,Description,LoadState,ActiveState,SubState,UnitFileState"});
	return result.success ? parse_systemctl_show(result.output) : std::vector<ServiceRecord>{};
}

bool ServiceManager::valid_service_name(const std::string_view name) {
	if (name.empty() || name.size() > 255 || name.front() == '-' || !ends_with(std::string(name), ".service")) {
		return false;
	}
	return std::all_of(name.begin(), name.end(), [](const unsigned char character) {
		return std::isalnum(character) != 0 || character == '-' || character == '_' || character == '.' || character == '@' || character == ':' ||
		       character == '\\';
	});
}

ServiceActionResult ServiceManager::run_action(const std::string_view action, const std::string& name) const {
	if (!available()) {
		return {false, "systemd is not available"};
	}
	if (!valid_service_name(name)) {
		return {false, "Invalid service unit name"};
	}
	const auto result = run_command_interactive({"systemctl", std::string(action), "--", name}, true);
	return {result.success, result.success ? std::string(action) + " completed for " + name : result.message};
}

ServiceActionResult ServiceManager::start(const std::string& name) const { return run_action("start", name); }
ServiceActionResult ServiceManager::stop(const std::string& name) const { return run_action("stop", name); }
ServiceActionResult ServiceManager::restart(const std::string& name) const { return run_action("restart", name); }
ServiceActionResult ServiceManager::enable(const std::string& name) const { return run_action("enable", name); }
ServiceActionResult ServiceManager::disable(const std::string& name) const { return run_action("disable", name); }

} // namespace apadana
