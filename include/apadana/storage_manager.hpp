#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace apadana {

struct FilesystemInfo {
		std::string device;
		std::string mount_point;
		std::string type;
		std::uint64_t total_bytes{};
		std::uint64_t available_bytes{};
};

class StorageManager {
	public:
		[[nodiscard]] bool available() const;
		// Real filesystems with capacity, sorted largest first.
		[[nodiscard]] std::vector<FilesystemInfo> list_filesystems() const;
};

// Parses /proc/self/mounts content, including octal-escaped paths.
std::vector<FilesystemInfo> parse_mounts(std::istream& input);
// True for pseudo filesystems that carry no persistent storage.
bool is_pseudo_filesystem(const std::string_view type);

} // namespace apadana
