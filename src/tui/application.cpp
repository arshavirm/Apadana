#include "apadana/tui.hpp"

#include "apadana/package_manager.hpp"
#include "apadana/process_manager.hpp"
#include "apadana/service_manager.hpp"
#include "apadana/system_info.hpp"
#include "apadana/user_manager.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <curses.h>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace apadana {
namespace {

enum class Page { Overview, Users, Packages, Processes, Services, Diagnostics };
enum class PackageView { Installed, Search, Upgradable };

struct PackageUiState {
		PackageView view{PackageView::Installed};
		std::vector<PackageRecord> packages;
		std::string query;
		std::size_t selected{};
		std::size_t offset{};
		bool loaded{};
};

template <typename Record> struct TableUiState {
		std::vector<Record> records;
		std::size_t selected{};
		std::size_t offset{};
};

constexpr std::array<std::pair<Page, std::string_view>, 6> pages{{
    {Page::Overview, "Overview"},
    {Page::Users, "Users"},
    {Page::Packages, "Packages"},
    {Page::Processes, "Processes"},
    {Page::Services, "Services"},
    {Page::Diagnostics, "Diagnostics"},
}};

constexpr int sidebar_width = 20;
constexpr int pair_accent   = 1;
constexpr int pair_sidebar  = 2;
constexpr int pair_selected = 3;
constexpr int pair_warning  = 4;
constexpr int pair_success  = 5;
constexpr int pair_danger   = 6;
constexpr int pair_muted    = 7;
constexpr int pair_heading  = 8;

void text_at(const int y, const int x, const std::string_view value, const int max_width = -1) {
	if (y < 0 || y >= LINES || x < 0 || x >= COLS) {
		return;
	}
	const int available = max_width >= 0 ? std::min(max_width, COLS - x) : COLS - x;
	if (available > 0) {
		mvaddnstr(y, x, value.data(), static_cast<int>(std::min(value.size(), static_cast<std::size_t>(available))));
	}
}

std::string percent_bar(const std::uint64_t used, const std::uint64_t total, const int width = 18) {
	const double ratio = total == 0 ? 0.0 : static_cast<double>(used) / static_cast<double>(total);
	const int filled   = std::clamp(static_cast<int>(ratio * width), 0, width);
	return "[" + std::string(static_cast<std::size_t>(filled), '#') + std::string(static_cast<std::size_t>(width - filled), '-') + "]";
}

void draw_frame(const Page active, const std::string& status) {
	erase();
	if (LINES < 18 || COLS < 72) {
		attron(A_BOLD | COLOR_PAIR(pair_warning));
		text_at(1, 2, "Terminal too small");
		attroff(A_BOLD | COLOR_PAIR(pair_warning));
		text_at(3, 2, "Resize to at least 72x18. Press q to quit.");
		refresh();
		return;
	}

	attron(COLOR_PAIR(pair_accent) | A_BOLD);
	text_at(0, 0, std::string(static_cast<std::size_t>(COLS), ' '));
	text_at(0, 2, "APADANA");
	text_at(0, 12, "SYSTEM CONTROL CENTER");
	attroff(COLOR_PAIR(pair_accent) | A_BOLD);

	attron(COLOR_PAIR(pair_sidebar));
	for (int row = 1; row < LINES - 1; ++row) {
		text_at(row, 0, std::string(sidebar_width, ' '));
	}
	attroff(COLOR_PAIR(pair_sidebar));

	attron(COLOR_PAIR(pair_sidebar) | A_BOLD);
	text_at(2, 2, "CONTROL PANEL");
	attroff(COLOR_PAIR(pair_sidebar) | A_BOLD);
	for (std::size_t index = 0; index < pages.size(); ++index) {
		const auto [page, label] = pages[index];
		if (page == active) {
			attron(COLOR_PAIR(pair_selected) | A_BOLD);
			text_at(4 + static_cast<int>(index) * 2, 1, "  " + std::string(label), sidebar_width - 2);
			attroff(COLOR_PAIR(pair_selected) | A_BOLD);
		} else {
			attron(COLOR_PAIR(pair_sidebar));
			text_at(4 + static_cast<int>(index) * 2, 3, label);
			attroff(COLOR_PAIR(pair_sidebar));
		}
	}

	attron(COLOR_PAIR(status.empty() ? pair_sidebar : pair_accent) | A_BOLD);
	text_at(LINES - 1, 0, std::string(static_cast<std::size_t>(COLS), ' '));
	text_at(LINES - 1, 2, status.empty() ? "arrows navigate  |  q quit" : status);
	attroff(COLOR_PAIR(status.empty() ? pair_sidebar : pair_accent) | A_BOLD);
}

void heading(const std::string_view title, const std::string_view subtitle) {
	attron(A_BOLD | COLOR_PAIR(pair_heading));
	text_at(2, sidebar_width + 3, title);
	attroff(A_BOLD | COLOR_PAIR(pair_heading));
	attron(A_DIM | COLOR_PAIR(pair_muted));
	text_at(3, sidebar_width + 3, subtitle, COLS - sidebar_width - 5);
	attroff(A_DIM | COLOR_PAIR(pair_muted));
}

void draw_overview(const SystemSnapshot& snapshot) {
	heading("Welcome to Apadana", "A clear view of your Unix system");
	const int left = sidebar_width + 3;
	const int info = left + 21;
	static constexpr std::array<std::string_view, 7> logo{
	    "       ___       ", "   ___/___\\___   ", "  |  _  _  _  |  ", "  | | || || | |  ", "  |_|_||_||_|_|  ", "  A P A D A N A  ", " system  manager ",
	};
	attron(COLOR_PAIR(pair_accent) | A_BOLD);
	for (std::size_t row = 0; row < logo.size(); ++row) {
		text_at(5 + static_cast<int>(row), left, logo[row], 19);
	}
	attroff(COLOR_PAIR(pair_accent) | A_BOLD);

	const char* login = std::getenv("USER");
	attron(A_BOLD);
	text_at(5, info, std::string(login != nullptr ? login : "user") + "@" + snapshot.platform.hostname);
	attroff(A_BOLD);
	text_at(6, info, std::string(36, '-'));
	text_at(7, info, "OS        " + snapshot.platform.distribution);
	text_at(8, info, "Kernel    " + snapshot.platform.kernel + " (" + snapshot.platform.architecture + ")");
	text_at(9, info, "Uptime    " + format_duration(snapshot.uptime_seconds));
	text_at(10, info, "CPU       " + std::to_string(snapshot.logical_cpu_count) + " logical cores");
	const char* shell      = std::getenv("SHELL");
	std::string shell_name = shell != nullptr ? shell : "unknown";
	if (const auto slash = shell_name.find_last_of('/'); slash != std::string::npos) {
		shell_name = shell_name.substr(slash + 1);
	}
	text_at(11, info, "Shell     " + shell_name);
	text_at(12, info,
		"Packages  " + std::to_string(snapshot.installed_package_count) + " installed / " + std::to_string(snapshot.package_managers.size()) +
		    " backends");

	const auto memory_used =
	    snapshot.memory.total_bytes >= snapshot.memory.available_bytes ? snapshot.memory.total_bytes - snapshot.memory.available_bytes : 0;
	const auto disk_used = snapshot.root_disk.capacity_bytes >= snapshot.root_disk.available_bytes
				   ? snapshot.root_disk.capacity_bytes - snapshot.root_disk.available_bytes
				   : 0;
	attron(A_BOLD);
	text_at(14, left, "SYSTEM HEALTH");
	attroff(A_BOLD);
	text_at(16, left,
		"Memory " + percent_bar(memory_used, snapshot.memory.total_bytes) + " " + format_bytes(memory_used) + " / " +
		    format_bytes(snapshot.memory.total_bytes));
	text_at(18, left,
		"Disk   " + percent_bar(disk_used, snapshot.root_disk.capacity_bytes) + " " + format_bytes(disk_used) + " / " +
		    format_bytes(snapshot.root_disk.capacity_bytes));
	text_at(20, left,
		"CPU    " + std::to_string(snapshot.logical_cpu_count) + " logical cores / load " + std::to_string(snapshot.load_average).substr(0, 4));
	if (LINES > 25) {
		text_at(22, left, "Network " + std::to_string(snapshot.network.active_interface_count) + "/" +
			std::to_string(snapshot.network.interface_count) + " active  RX " + format_bytes(snapshot.network.received_bytes) +
			"  TX " + format_bytes(snapshot.network.transmitted_bytes));
		text_at(23, left, "Security ASLR " + std::string(snapshot.security.address_space_randomization ? "on" : "off") +
			"  firewall tool " + std::string(snapshot.security.firewall_tool_available ? "found" : "not found"));
	}

	if (LINES > 23) {
		attron(COLOR_PAIR(pair_accent));
		text_at(22, left, "  ");
		attroff(COLOR_PAIR(pair_accent));
		attron(COLOR_PAIR(pair_selected));
		text_at(22, left + 3, "  ");
		attroff(COLOR_PAIR(pair_selected));
		attron(COLOR_PAIR(pair_warning));
		text_at(22, left + 6, "  ");
		attroff(COLOR_PAIR(pair_warning));
	}
	attron(A_DIM);
	text_at(LINES - 2, left, "r refresh system snapshot");
	attroff(A_DIM);
}

bool confirm(const std::string& question) {
	attron(COLOR_PAIR(pair_warning) | A_BOLD);
	text_at(LINES - 1, 0, std::string(static_cast<std::size_t>(COLS), ' '));
	text_at(LINES - 1, 2, question + " [y/N]");
	attroff(COLOR_PAIR(pair_warning) | A_BOLD);
	refresh();
	const int key = getch();
	return key == 'y' || key == 'Y';
}

std::string prompt_text(const std::string& label, const std::size_t maximum_length) {
	std::vector<char> buffer(maximum_length + 1, '\0');
	attron(COLOR_PAIR(pair_accent) | A_BOLD);
	text_at(LINES - 1, 0, std::string(static_cast<std::size_t>(COLS), ' '));
	text_at(LINES - 1, 2, label);
	attroff(COLOR_PAIR(pair_accent) | A_BOLD);
	echo();
	curs_set(1);
	move(LINES - 1, 2 + static_cast<int>(label.size()));
	getnstr(buffer.data(), static_cast<int>(maximum_length));
	noecho();
	curs_set(0);
	return buffer.data();
}

void suspend_terminal() {
	def_prog_mode();
	endwin();
}

void resume_terminal() {
	reset_prog_mode();
	refresh();
}

void draw_users(const std::vector<UserAccount>& users, const UserManager& manager, const std::size_t selected, const std::size_t offset) {
	heading("User accounts", "Local identities / backend: " + manager.backend_name());
	const int left = sidebar_width + 3;
	attron(A_BOLD | COLOR_PAIR(pair_accent));
	text_at(5, left, "USERNAME", 22);
	text_at(5, left + 23, "UID", 8);
	text_at(5, left + 32, "TYPE", 12);
	text_at(5, left + 45, "STATE", 12);
	text_at(5, left + 58, "HOME", COLS - left - 60);
	attroff(A_BOLD | COLOR_PAIR(pair_accent));

	const int visible = std::max(1, LINES - 9);
	for (int row = 0; row < visible && offset + static_cast<std::size_t>(row) < users.size(); ++row) {
		const std::size_t index = offset + static_cast<std::size_t>(row);
		const auto& user	= users[index];
		if (index == selected) {
			attron(COLOR_PAIR(pair_selected) | A_BOLD);
			text_at(6 + row, left - 1, std::string(static_cast<std::size_t>(COLS - left), ' '));
		}
		text_at(6 + row, left, user.name, 21);
		text_at(6 + row, left + 23, std::to_string(user.uid), 8);
		text_at(6 + row, left + 32, user.uid == 0 ? "root" : (user.system_account ? "system" : "login"), 12);
		const std::string state = !user.locked.has_value() ? "unknown" : (*user.locked ? "locked" : "active");
		const int state_pair	= !user.locked.has_value() ? pair_muted : (*user.locked ? pair_danger : pair_success);
		if (index != selected) attron(COLOR_PAIR(state_pair) | A_BOLD);
		text_at(6 + row, left + 45, state, 12);
		if (index != selected) attroff(COLOR_PAIR(state_pair) | A_BOLD);
		text_at(6 + row, left + 58, user.home, COLS - left - 60);
		if (index == selected) {
			attroff(COLOR_PAIR(pair_selected) | A_BOLD);
		}
	}
	attron(A_DIM);
	text_at(LINES - 2, left, "a add  |  l lock  |  u unlock  |  r refresh");
	attroff(A_DIM);
}

std::string package_view_title(const PackageUiState& state) {
	switch (state.view) {
	case PackageView::Installed:
		return "Installed packages";
	case PackageView::Search:
		return "Search: " + state.query;
	case PackageView::Upgradable:
		return "Available upgrades";
	}
	return "Packages";
}

void draw_packages(const PackageUiState& state, const AptPackageManager& apt) {
	if (!apt.available()) {
		heading("Packages", "APT backend is not available on this host");
		attron(COLOR_PAIR(pair_warning) | A_BOLD);
		text_at(7, sidebar_width + 3, "NO SUPPORTED PACKAGE BACKEND");
		attroff(COLOR_PAIR(pair_warning) | A_BOLD);
		text_at(9, sidebar_width + 3, "Apadana currently implements APT; more distro backends will follow.");
		return;
	}

	heading(package_view_title(state), "APT package control / " + std::to_string(state.packages.size()) + " results");
	const int left = sidebar_width + 3;
	attron(A_BOLD | COLOR_PAIR(pair_accent));
	text_at(5, left, "PACKAGE", 28);
	text_at(5, left + 29, "VERSION", 24);
	text_at(5, left + 54, "DESCRIPTION", COLS - left - 55);
	attroff(A_BOLD | COLOR_PAIR(pair_accent));

	const int visible = std::max(1, LINES - 9);
	for (int row = 0; row < visible && state.offset + static_cast<std::size_t>(row) < state.packages.size(); ++row) {
		const std::size_t index = state.offset + static_cast<std::size_t>(row);
		const auto& package	= state.packages[index];
		if (index == state.selected) {
			attron(COLOR_PAIR(pair_selected) | A_BOLD);
			text_at(6 + row, left - 1, std::string(static_cast<std::size_t>(COLS - left), ' '));
		}
		text_at(6 + row, left, package.name, 28);
		if (index != state.selected) attron(COLOR_PAIR(package.upgradable ? pair_warning : (package.installed ? pair_success : pair_muted)));
		text_at(6 + row, left + 29, package.version.empty() ? (package.installed ? "installed" : "available") : package.version, 24);
		if (index != state.selected) attroff(COLOR_PAIR(package.upgradable ? pair_warning : (package.installed ? pair_success : pair_muted)));
		text_at(6 + row, left + 54, package.description, COLS - left - 55);
		if (index == state.selected) {
			attroff(COLOR_PAIR(pair_selected) | A_BOLD);
		}
	}
	if (state.packages.empty()) {
		attron(A_DIM);
		text_at(8, left, state.loaded ? "No packages found." : "Loading package information...");
		attroff(A_DIM);
	}
	attron(A_DIM);
	text_at(LINES - 2, left, "s search | a installed | g upgrades | i install | d remove | u update | U upgrade all");
	attroff(A_DIM);
}

std::string format_percent(const double value) {
	std::ostringstream output;
	output.setf(std::ios::fixed);
	output.precision(1);
	output << value << '%';
	return output.str();
}

void draw_processes(const TableUiState<ProcessRecord>& state, const ProcessManager& manager) {
	if (!manager.available()) {
		heading("Processes", "Process inspection requires a procfs-compatible host");
		attron(COLOR_PAIR(pair_warning) | A_BOLD);
		text_at(7, sidebar_width + 3, "PROCESS BACKEND UNAVAILABLE");
		attroff(COLOR_PAIR(pair_warning) | A_BOLD);
		return;
	}

	heading("Processes", std::to_string(state.records.size()) + " processes / sorted by lifetime CPU usage");
	const int left = sidebar_width + 3;
	attron(A_BOLD | COLOR_PAIR(pair_accent));
	text_at(5, left, "PID", 7);
	text_at(5, left + 8, "USER", 11);
	text_at(5, left + 20, "STATE", 9);
	text_at(5, left + 30, "CPU", 7);
	text_at(5, left + 38, "MEMORY", 10);
	text_at(5, left + 49, "COMMAND", COLS - left - 50);
	attroff(A_BOLD | COLOR_PAIR(pair_accent));

	const int visible = std::max(1, LINES - 9);
	for (int row = 0; row < visible && state.offset + static_cast<std::size_t>(row) < state.records.size(); ++row) {
		const std::size_t index = state.offset + static_cast<std::size_t>(row);
		const auto& process	= state.records[index];
		const int state_pair	= process.state == 'R' ? pair_success : (process.state == 'Z' ? pair_danger : pair_muted);
		if (index == state.selected) {
			attron(COLOR_PAIR(pair_selected) | A_BOLD);
			text_at(6 + row, left - 1, std::string(static_cast<std::size_t>(COLS - left), ' '));
		}
		text_at(6 + row, left, std::to_string(process.pid), 7);
		text_at(6 + row, left + 8, process.user, 11);
		if (index != state.selected) attron(COLOR_PAIR(state_pair));
		text_at(6 + row, left + 20, process_state_name(process.state), 9);
		if (index != state.selected) attroff(COLOR_PAIR(state_pair));
		text_at(6 + row, left + 30, format_percent(process.cpu_percent), 7);
		text_at(6 + row, left + 38, format_bytes(process.resident_bytes), 10);
		text_at(6 + row, left + 49, process.command, COLS - left - 50);
		if (index == state.selected) attroff(COLOR_PAIR(pair_selected) | A_BOLD);
	}
	attron(A_DIM | COLOR_PAIR(pair_muted));
	text_at(LINES - 2, left, "r refresh | t terminate | K force kill | running=green zombie=red");
	attroff(A_DIM | COLOR_PAIR(pair_muted));
}

void draw_services(const TableUiState<ServiceRecord>& state, const ServiceManager& manager) {
	if (!manager.available()) {
		heading("Services", "No supported service manager was detected");
		attron(COLOR_PAIR(pair_warning) | A_BOLD);
		text_at(7, sidebar_width + 3, "SYSTEMD UNAVAILABLE");
		attroff(COLOR_PAIR(pair_warning) | A_BOLD);
		return;
	}

	heading("Services", manager.backend_name() + " service control / " + std::to_string(state.records.size()) + " units");
	const int left = sidebar_width + 3;
	attron(A_BOLD | COLOR_PAIR(pair_accent));
	text_at(5, left, "SERVICE", 25);
	text_at(5, left + 26, "ACTIVE", 10);
	text_at(5, left + 37, "SUB", 10);
	text_at(5, left + 48, "STARTUP", 10);
	text_at(5, left + 59, "DESCRIPTION", COLS - left - 60);
	attroff(A_BOLD | COLOR_PAIR(pair_accent));

	const int visible = std::max(1, LINES - 9);
	for (int row = 0; row < visible && state.offset + static_cast<std::size_t>(row) < state.records.size(); ++row) {
		const std::size_t index = state.offset + static_cast<std::size_t>(row);
		const auto& service	= state.records[index];
		const int active_pair	= service.active_state == "active" ? pair_success : (service.active_state == "failed" ? pair_danger : pair_muted);
		if (index == state.selected) {
			attron(COLOR_PAIR(pair_selected) | A_BOLD);
			text_at(6 + row, left - 1, std::string(static_cast<std::size_t>(COLS - left), ' '));
		}
		text_at(6 + row, left, service.name, 25);
		if (index != state.selected) attron(COLOR_PAIR(active_pair) | A_BOLD);
		text_at(6 + row, left + 26, service.active_state, 10);
		if (index != state.selected) attroff(COLOR_PAIR(active_pair) | A_BOLD);
		text_at(6 + row, left + 37, service.sub_state, 10);
		text_at(6 + row, left + 48, service.unit_file_state.empty() ? "unknown" : service.unit_file_state, 10);
		text_at(6 + row, left + 59, service.description, COLS - left - 60);
		if (index == state.selected) attroff(COLOR_PAIR(pair_selected) | A_BOLD);
	}
	if (state.records.empty()) {
		attron(COLOR_PAIR(pair_warning));
		text_at(8, left, "No services returned. systemd may not be running on this host.");
		attroff(COLOR_PAIR(pair_warning));
	}
	attron(A_DIM | COLOR_PAIR(pair_muted));
	text_at(LINES - 2, left, "r refresh | s start | x stop | R restart | e enable | d disable");
	attroff(A_DIM | COLOR_PAIR(pair_muted));
}

void draw_diagnostics(const SystemSnapshot& snapshot) {
	heading("Diagnostics", "Read-only network and security posture");
	const int left = sidebar_width + 3;
	attron(A_BOLD | COLOR_PAIR(pair_accent));
	text_at(5, left, "NETWORK SUMMARY");
	attroff(A_BOLD | COLOR_PAIR(pair_accent));
	text_at(7, left, "Interfaces       " + std::to_string(snapshot.network.interface_count));
	text_at(8, left, "Active           " + std::to_string(snapshot.network.active_interface_count));
	text_at(9, left, "Received         " + format_bytes(snapshot.network.received_bytes));
	text_at(10, left, "Transmitted      " + format_bytes(snapshot.network.transmitted_bytes));

	attron(A_BOLD | COLOR_PAIR(pair_accent));
	text_at(13, left, "SECURITY POSTURE");
	attroff(A_BOLD | COLOR_PAIR(pair_accent));
	const int aslr_pair = snapshot.security.address_space_randomization ? pair_success : pair_danger;
	attron(COLOR_PAIR(aslr_pair) | A_BOLD);
	text_at(15, left, std::string("ASLR             ") + (snapshot.security.address_space_randomization ? "enabled" : "disabled"));
	attroff(COLOR_PAIR(aslr_pair) | A_BOLD);
	const int firewall_pair = snapshot.security.firewall_tool_available ? pair_success : pair_warning;
	attron(COLOR_PAIR(firewall_pair) | A_BOLD);
	text_at(16, left, std::string("Firewall tooling  ") + (snapshot.security.firewall_tool_available ? "detected" : "not detected"));
	attroff(COLOR_PAIR(firewall_pair) | A_BOLD);
	attron(A_DIM | COLOR_PAIR(pair_muted));
	text_at(19, left, "These checks describe host configuration; they do not change system state.");
	text_at(LINES - 2, left, "r refresh diagnostics");
	attroff(A_DIM | COLOR_PAIR(pair_muted));
}

void reset_package_selection(PackageUiState& state) {
	state.selected = 0;
	state.offset   = 0;
}

void load_package_view(PackageUiState& state, const AptPackageManager& apt) {
	switch (state.view) {
	case PackageView::Installed:
		state.packages = apt.installed_packages();
		break;
	case PackageView::Search:
		state.packages = apt.search(state.query);
		break;
	case PackageView::Upgradable:
		state.packages = apt.upgradable_packages();
		break;
	}
	state.loaded = true;
	reset_package_selection(state);
}

class CursesSession {
	public:
		CursesSession() {
			initscr();
			cbreak();
			noecho();
			keypad(stdscr, TRUE);
			curs_set(0);
			if (has_colors()) {
				start_color();
				use_default_colors();
				init_pair(pair_accent, COLOR_WHITE, COLOR_BLUE);
				init_pair(pair_sidebar, COLOR_WHITE, COLOR_BLACK);
				init_pair(pair_selected, COLOR_BLACK, COLOR_CYAN);
				init_pair(pair_warning, COLOR_YELLOW, -1);
				init_pair(pair_success, COLOR_GREEN, -1);
				init_pair(pair_danger, COLOR_RED, -1);
				init_pair(pair_muted, COLOR_CYAN, -1);
				init_pair(pair_heading, COLOR_MAGENTA, -1);
			}
		}
		~CursesSession() { endwin(); }
};

} // namespace

int Application::run() {
	CursesSession terminal;
	auto snapshot = collect_system_snapshot();
	UserManager user_manager;
	AptPackageManager apt;
	ProcessManager process_manager;
	ServiceManager service_manager;
	auto users = user_manager.list_users();
	PackageUiState package_state;
	TableUiState<ProcessRecord> process_state;
	TableUiState<ServiceRecord> service_state;
	bool processes_loaded	  = false;
	bool services_loaded	  = false;
	std::size_t page_index	  = 0;
	std::size_t selected_user = 0;
	std::size_t user_offset	  = 0;
	std::string status;

	bool running = true;
	while (running) {
		const Page page = pages[page_index].first;
		if (page == Page::Packages && apt.available() && !package_state.loaded) {
			load_package_view(package_state, apt);
		}
		if (page == Page::Processes && !processes_loaded) {
			process_state.records = process_manager.list_processes();
			processes_loaded      = true;
		}
		if (page == Page::Services && !services_loaded) {
			service_state.records = service_manager.list_services();
			services_loaded	      = true;
		}
		draw_frame(page, status);
		if (LINES >= 18 && COLS >= 72) {
			if (page == Page::Overview) {
				draw_overview(snapshot);
			} else if (page == Page::Users) {
				draw_users(users, user_manager, selected_user, user_offset);
			} else if (page == Page::Packages) {
				draw_packages(package_state, apt);
			} else if (page == Page::Processes) {
				draw_processes(process_state, process_manager);
			} else if (page == Page::Services) {
				draw_services(service_state, service_manager);
			} else {
				draw_diagnostics(snapshot);
			}
		}
		refresh();
		status.clear();

		const int key = getch();
		if (key == 'q' || key == 'Q') {
			running = false;
		} else if (key == KEY_LEFT) {
			page_index = page_index == 0 ? pages.size() - 1 : page_index - 1;
		} else if (key == KEY_RIGHT || key == '\t') {
			page_index = (page_index + 1) % pages.size();
		} else if (page == Page::Overview && key == 'r') {
			snapshot = collect_system_snapshot();
			status	 = "System snapshot refreshed";
		} else if (page == Page::Users && (key == KEY_UP || key == 'k') && selected_user > 0) {
			--selected_user;
		} else if (page == Page::Users && (key == KEY_DOWN || key == 'j') && selected_user + 1 < users.size()) {
			++selected_user;
		} else if (page == Page::Users && key == 'r') {
			users	      = user_manager.list_users();
			selected_user = std::min(selected_user, users.empty() ? std::size_t{0} : users.size() - 1);
			status	      = "User list refreshed";
		} else if (page == Page::Users && key == 'a') {
			const auto username = prompt_text("New username: ", 32);
			if (!UserManager::valid_username(username)) {
				status = "Invalid username";
			} else if (confirm("Create account '" + username + "' with a home directory?")) {
				suspend_terminal();
				const auto result = user_manager.create_user(username);
				resume_terminal();
				status = result.message;
				users  = user_manager.list_users();
			}
		} else if (page == Page::Users && !users.empty() && (key == 'l' || key == 'u')) {
			const auto username = users[selected_user].name;
			const bool locking  = key == 'l';
			if (confirm(std::string(locking ? "Lock" : "Unlock") + " account '" + username + "'?")) {
				suspend_terminal();
				const auto result = locking ? user_manager.lock_user(username) : user_manager.unlock_user(username);
				resume_terminal();
				status = result.message;
				users  = user_manager.list_users();
			}
		} else if (page == Page::Packages && apt.available() && (key == KEY_UP || key == 'k') && package_state.selected > 0) {
			--package_state.selected;
		} else if (page == Page::Packages && apt.available() && (key == KEY_DOWN || key == 'j') &&
			   package_state.selected + 1 < package_state.packages.size()) {
			++package_state.selected;
		} else if (page == Page::Packages && apt.available() && key == 's') {
			const auto query = prompt_text("Search APT: ", 100);
			if (!query.empty()) {
				package_state.view  = PackageView::Search;
				package_state.query = query;
				load_package_view(package_state, apt);
				status = "APT search completed";
			}
		} else if (page == Page::Packages && apt.available() && key == 'a') {
			package_state.view = PackageView::Installed;
			load_package_view(package_state, apt);
			status = "Showing installed packages";
		} else if (page == Page::Packages && apt.available() && key == 'g') {
			package_state.view = PackageView::Upgradable;
			load_package_view(package_state, apt);
			status = "Showing available upgrades";
		} else if (page == Page::Packages && apt.available() && key == 'r') {
			load_package_view(package_state, apt);
			status = "Package view refreshed";
		} else if (page == Page::Packages && apt.available() && key == 'u') {
			if (confirm("Refresh the APT package index?")) {
				suspend_terminal();
				const auto result = apt.update_index();
				resume_terminal();
				status = result.message;
				load_package_view(package_state, apt);
			}
		} else if (page == Page::Packages && apt.available() && key == 'U') {
			if (confirm("Upgrade all packages using APT?")) {
				suspend_terminal();
				const auto result = apt.upgrade_all();
				resume_terminal();
				status = result.message;
				load_package_view(package_state, apt);
			}
		} else if (page == Page::Packages && apt.available() && !package_state.packages.empty() && key == 'i') {
			const auto package = package_state.packages[package_state.selected].name;
			if (confirm("Install package '" + package + "'?")) {
				suspend_terminal();
				const auto result = apt.install(package);
				resume_terminal();
				status = result.message;
				load_package_view(package_state, apt);
			}
		} else if (page == Page::Packages && apt.available() && !package_state.packages.empty() && key == 'd') {
			const auto package = package_state.packages[package_state.selected].name;
			if (!package_state.packages[package_state.selected].installed) {
				status = "Select an installed package before removing";
			} else if (confirm("Remove package '" + package + "'?")) {
				suspend_terminal();
				const auto result = apt.remove(package);
				resume_terminal();
				status = result.message;
				load_package_view(package_state, apt);
			}
		} else if (page == Page::Processes && (key == KEY_UP || key == 'k') && process_state.selected > 0) {
			--process_state.selected;
		} else if (page == Page::Processes && (key == KEY_DOWN || key == 'j') && process_state.selected + 1 < process_state.records.size()) {
			++process_state.selected;
		} else if (page == Page::Processes && key == 'r') {
			process_state.records = process_manager.list_processes();
			process_state.selected =
			    std::min(process_state.selected, process_state.records.empty() ? std::size_t{0} : process_state.records.size() - 1);
			status = "Process list refreshed";
		} else if (page == Page::Processes && !process_state.records.empty() && (key == 't' || key == 'K')) {
			const auto process = process_state.records[process_state.selected];
			const bool force   = key == 'K';
			if (confirm(std::string(force ? "Force kill" : "Terminate") + " PID " + std::to_string(process.pid) + " (" + process.command + ")?")) {
				const auto result     = process_manager.terminate(process.pid, force);
				status		      = result.message;
				process_state.records = process_manager.list_processes();
				process_state.selected =
				    std::min(process_state.selected, process_state.records.empty() ? std::size_t{0} : process_state.records.size() - 1);
			}
		} else if (page == Page::Diagnostics && key == 'r') {
			snapshot = collect_system_snapshot();
			status = "Diagnostics refreshed";
		} else if (page == Page::Services && (key == KEY_UP || key == 'k') && service_state.selected > 0) {
			--service_state.selected;
		} else if (page == Page::Services && (key == KEY_DOWN || key == 'j') && service_state.selected + 1 < service_state.records.size()) {
			++service_state.selected;
		} else if (page == Page::Services && key == 'r') {
			service_state.records = service_manager.list_services();
			service_state.selected =
			    std::min(service_state.selected, service_state.records.empty() ? std::size_t{0} : service_state.records.size() - 1);
			status = "Service list refreshed";
		} else if (page == Page::Services && !service_state.records.empty() && (key == 's' || key == 'x' || key == 'R' || key == 'e' || key == 'd')) {
			const auto service = service_state.records[service_state.selected].name;
			std::string action;
			if (key == 's')
				action = "Start";
			else if (key == 'x')
				action = "Stop";
			else if (key == 'R')
				action = "Restart";
			else if (key == 'e')
				action = "Enable";
			else
				action = "Disable";
			if (confirm(action + " service '" + service + "'?")) {
				suspend_terminal();
				ServiceActionResult result;
				if (key == 's')
					result = service_manager.start(service);
				else if (key == 'x')
					result = service_manager.stop(service);
				else if (key == 'R')
					result = service_manager.restart(service);
				else if (key == 'e')
					result = service_manager.enable(service);
				else
					result = service_manager.disable(service);
				resume_terminal();
				status		      = result.message;
				service_state.records = service_manager.list_services();
				service_state.selected =
				    std::min(service_state.selected, service_state.records.empty() ? std::size_t{0} : service_state.records.size() - 1);
			}
		}

		const std::size_t visible = static_cast<std::size_t>(std::max(1, LINES - 9));
		if (selected_user < user_offset) {
			user_offset = selected_user;
		} else if (selected_user >= user_offset + visible) {
			user_offset = selected_user - visible + 1;
		}
		if (package_state.selected < package_state.offset) {
			package_state.offset = package_state.selected;
		} else if (package_state.selected >= package_state.offset + visible) {
			package_state.offset = package_state.selected - visible + 1;
		}
		if (process_state.selected < process_state.offset) {
			process_state.offset = process_state.selected;
		} else if (process_state.selected >= process_state.offset + visible) {
			process_state.offset = process_state.selected - visible + 1;
		}
		if (service_state.selected < service_state.offset) {
			service_state.offset = service_state.selected;
		} else if (service_state.selected >= service_state.offset + visible) {
			service_state.offset = service_state.selected - visible + 1;
		}
	}
	return 0;
}

} // namespace apadana
