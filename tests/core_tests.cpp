#include "apadana/command.hpp"
#include "apadana/package_manager.hpp"
#include "apadana/platform.hpp"
#include "apadana/process_manager.hpp"
#include "apadana/service_manager.hpp"
#include "apadana/system_info.hpp"
#include "apadana/user_manager.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

int failures = 0;

void expect(const bool condition, const std::string& message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

} // namespace

int main() {
	std::istringstream release(R"(
# comment
NAME="Example Linux"
PRETTY_NAME="Example Linux 42"
ID=example
VERSION_ID="42"
)");
	const auto platform = apadana::parse_os_release(release);
	expect(platform.distribution == "Example Linux 42", "parse PRETTY_NAME");
	expect(platform.distribution_id == "example", "parse distribution ID");
	expect(platform.version == "42", "parse VERSION_ID");

	expect(apadana::unquote_os_release_value("\"hello world\"") == "hello world", "unquote value");
	expect(apadana::format_bytes(0) == "0 B", "format zero bytes");
	expect(apadana::format_bytes(1536) == "1.5 KiB", "format kibibytes");
	expect(apadana::format_duration(60) == "1m", "format minutes");
	expect(apadana::format_duration(90060) == "1d 1h 1m", "format duration");
	const auto snapshot = apadana::collect_system_snapshot();
	expect(snapshot.logical_cpu_count > 0, "collect logical CPU count");
	expect(apadana::executable_on_path("sh", "/bin:/usr/bin"), "find executable on explicit PATH");
	expect(!apadana::executable_on_path("not-a-real-apadana-command", "/bin"), "missing executable");
	expect(apadana::UserManager::valid_username("alice"), "valid username");
	expect(apadana::UserManager::valid_username("service_user-2"), "valid service username");
	expect(!apadana::UserManager::valid_username("Alice"), "reject uppercase username");
	expect(!apadana::UserManager::valid_username("bad;name"), "reject shell punctuation");
	expect(!apadana::UserManager::valid_username(""), "reject empty username");
	expect(apadana::AptPackageManager::valid_package_name("libc6:amd64"), "valid architecture-qualified package");
	expect(apadana::AptPackageManager::valid_package_name("libstdc++6"), "valid Debian package name");
	expect(!apadana::AptPackageManager::valid_package_name("bad package"), "reject package whitespace");
	expect(!apadana::AptPackageManager::valid_package_name("--option"), "reject option-like package name");

	const auto installed = apadana::parse_dpkg_query("bash\t5.2\tGNU shell\ncoreutils\t9.4\tGNU utilities\n");
	expect(installed.size() == 2, "parse installed package count");
	expect(installed[0].name == "bash" && installed[0].installed, "parse installed package");
	expect(installed[1].description == "GNU utilities", "parse installed package description");

	const auto search = apadana::parse_apt_cache_search("htop - interactive process viewer\ninvalid line\n");
	expect(search.size() == 1, "parse APT search count");
	expect(search[0].name == "htop" && search[0].description == "interactive process viewer", "parse APT search item");

	const auto upgrades = apadana::parse_apt_upgradable("Listing...\nbase-files/noble-updates 13.1 amd64 [upgradable from: 13.0]\n");
	expect(upgrades.size() == 1, "parse upgrade count");
	expect(upgrades[0].name == "base-files" && upgrades[0].version == "13.1" && upgrades[0].upgradable, "parse upgrade item");

	const auto captured = apadana::run_command_capture({"printf", "%s", "safe;literal"});
	expect(captured.success && captured.output == "safe;literal", "execute arguments without shell interpretation");

	char process_state	    = '?';
	std::uint64_t cpu_ticks	    = 0;
	std::int64_t resident_pages = 0;
	const std::string proc_stat = "42 (worker thread) R 1 2 3 4 5 6 7 8 9 10 100 25 13 14 15 16 17 18 19 20 256";
	expect(apadana::parse_proc_stat(proc_stat, process_state, cpu_ticks, resident_pages), "parse proc stat");
	expect(process_state == 'R' && cpu_ticks == 125 && resident_pages == 256, "extract proc stat metrics");
	expect(apadana::process_state_name('Z') == "zombie", "format process state");
	std::istringstream command_line(std::string("python\0worker.py\0", 17));
	expect(apadana::read_process_command(command_line, "fallback") == "python worker.py", "parse process command line");
	std::istringstream empty_command;
	expect(apadana::read_process_command(empty_command, "kernel-thread") == "kernel-thread", "fall back to process comm");
	apadana::ProcessManager process_manager;
	if (process_manager.available()) {
		expect(!process_manager.list_processes().empty(), "discover live processes from procfs");
	}

	const auto services = apadana::parse_systemctl_show(
	    "Id=cron.service\nDescription=Job scheduler\nLoadState=loaded\nActiveState=active\nSubState=running\nUnitFileState=enabled\n\n"
	    "Id=broken.service\nDescription=Broken unit\nLoadState=loaded\nActiveState=failed\nSubState=failed\nUnitFileState=disabled\n");
	expect(services.size() == 2, "parse systemd service count");
	expect(services[0].name == "broken.service" && services[0].active_state == "failed", "sort and parse systemd service");
	expect(services[1].unit_file_state == "enabled", "parse service startup state");
	expect(apadana::ServiceManager::valid_service_name("ssh@server.service"), "valid systemd service name");
	expect(!apadana::ServiceManager::valid_service_name("--bad.service"), "reject option-like systemd service name");
	expect(!apadana::ServiceManager::valid_service_name("bad service.service"), "reject service whitespace");

	if (failures == 0) {
		std::cout << "All tests passed\n";
	}
	return failures == 0 ? 0 : 1;
}
