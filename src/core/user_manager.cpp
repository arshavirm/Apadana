#include "apadana/user_manager.hpp"

#include "apadana/command.hpp"
#include "apadana/platform.hpp"

#include <algorithm>
#include <cerrno>
#include <pwd.h>
#if defined(__linux__)
#include <shadow.h>
#endif

namespace apadana {
namespace {

std::optional<bool> account_locked(const std::string& username) {
#if defined(__linux__)
	errno		   = 0;
	const spwd* shadow = ::getspnam(username.c_str());
	if (shadow == nullptr || shadow->sp_pwdp == nullptr) {
		return std::nullopt;
	}
	const std::string password(shadow->sp_pwdp);
	return password.empty() || password.front() == '!' || password.front() == '*';
#else
	static_cast<void>(username);
	return std::nullopt;
#endif
}

} // namespace

std::vector<UserAccount> UserManager::list_users() const {
	std::vector<UserAccount> users;
	::setpwent();
	while (const passwd* entry = ::getpwent()) {
		UserAccount user;
		user.name	    = entry->pw_name != nullptr ? entry->pw_name : "";
		user.uid	    = static_cast<std::uint32_t>(entry->pw_uid);
		user.gid	    = static_cast<std::uint32_t>(entry->pw_gid);
		user.home	    = entry->pw_dir != nullptr ? entry->pw_dir : "";
		user.shell	    = entry->pw_shell != nullptr ? entry->pw_shell : "";
		user.locked	    = account_locked(user.name);
		user.system_account = user.uid < 1000 && user.uid != 0;
		users.push_back(std::move(user));
	}
	::endpwent();

	std::sort(users.begin(), users.end(), [](const UserAccount& left, const UserAccount& right) { return left.uid < right.uid; });
	return users;
}

bool UserManager::can_manage_users() const { return (executable_on_path("useradd") && executable_on_path("usermod")) || executable_on_path("pw"); }

std::string UserManager::backend_name() const {
	if (executable_on_path("useradd") && executable_on_path("usermod")) {
		return "shadow-utils";
	}
	if (executable_on_path("pw")) {
		return "FreeBSD pw";
	}
	return "read-only";
}

bool UserManager::valid_username(const std::string& username) {
	if (username.empty() || username.size() > 32) {
		return false;
	}
	const auto valid_first = [](const unsigned char character) { return (character >= 'a' && character <= 'z') || character == '_'; };
	const auto valid_rest  = [](const unsigned char character) {
		return (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
	};
	return valid_first(static_cast<unsigned char>(username.front())) &&
	       std::all_of(username.begin() + 1, username.end(), valid_rest);
}

ActionResult UserManager::create_user(const std::string& username) const {
	if (!valid_username(username)) {
		return {false, "Invalid username: use lowercase letters, digits, '_' or '-'"};
	}
	if (executable_on_path("useradd")) {
		return run_privileged({"useradd", "--create-home", username});
	}
	if (executable_on_path("pw")) {
		return run_privileged({"pw", "useradd", username, "-m"});
	}
	return {false, "No supported user-management backend was detected"};
}

ActionResult UserManager::lock_user(const std::string& username) const {
	if ((!valid_username(username) && username != "root") || username == "root") {
		return {false, username == "root" ? "Apadana will not lock the root account" : "Invalid account name"};
	}
	if (executable_on_path("usermod")) {
		return run_privileged({"usermod", "--lock", username});
	}
	if (executable_on_path("pw")) {
		return run_privileged({"pw", "lock", username});
	}
	return {false, "No supported user-management backend was detected"};
}

ActionResult UserManager::unlock_user(const std::string& username) const {
	if (!valid_username(username) && username != "root") {
		return {false, "Invalid account name"};
	}
	if (executable_on_path("usermod")) {
		return run_privileged({"usermod", "--unlock", username});
	}
	if (executable_on_path("pw")) {
		return run_privileged({"pw", "unlock", username});
	}
	return {false, "No supported user-management backend was detected"};
}

ActionResult UserManager::run_privileged(const std::vector<std::string>& arguments) const {
	const auto result = run_command_interactive(arguments, true);
	return {result.success, result.success ? "Account action completed successfully" : result.message};
}

} // namespace apadana
