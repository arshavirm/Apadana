#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace apadana {

struct UserAccount {
		std::string name;
		std::uint32_t uid{};
		std::uint32_t gid{};
		std::string home;
		std::string shell;
		std::optional<bool> locked;
		bool system_account{};
};

struct ActionResult {
		bool success{};
		std::string message;
};

class UserManager {
	public:
		[[nodiscard]] std::vector<UserAccount> list_users() const;
		[[nodiscard]] bool can_manage_users() const;
		[[nodiscard]] std::string backend_name() const;

		ActionResult create_user(const std::string& username) const;
		ActionResult lock_user(const std::string& username) const;
		ActionResult unlock_user(const std::string& username) const;

		static bool valid_username(const std::string& username);

	private:
		ActionResult run_privileged(const std::vector<std::string>& arguments) const;
};

} // namespace apadana
