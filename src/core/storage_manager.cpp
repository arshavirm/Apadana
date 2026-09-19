#include "apadana/storage_manager.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sys/statvfs.h>

namespace apadana {
namespace {

constexpr std::array<std::string_view, 30> pseudo_filesystems{
    "proc",	  "sysfs",	 "devtmpfs", "devpts",  "securityfs", "cgroup",	   "cgroup2",   "pstore",
    "bpf",	  "debugfs",	 "tracefs",  "configfs", "fusectl",   "mqueue",	   "hugetlbfs", "ramfs",
    "efivarfs", "bpf_fs",	 "autofs",   "binfmt_misc", "configfs", "rpc_pipefs", "selinuxfs", "systemd-1",
    "nsfs",	  "overlay", "squashfs",
};

std::string decode_mount_path(const std::string_view value) {
	// /proc/self/mounts escapes spaces as \040, tabs as \011, and so on.
	std::string path;
	for (std::size_t index = 0; index < value.size(); ++index) {
		if (value[index] == '\\' && index + 3 < value.size() && value[index + 1] == '0') {
			const auto digit = [](const char character) { return character >= '0' && character <= '7' ? character - '0' : -1; };
			const int first = digit(value[index + 1]);
			const int second = digit(value[index + 2]);
			const int third = digit(value[index + 3]);
			if (first >= 0 && second >= 0 && third >= 0) {
				path.push_back(static_cast<char>(first * 64 + second * 8 + third));
				index += 3;
				continue;
			}
		}
		path.push_back(value[index]);
	}
	return path;
}

} // namespace

bool is_pseudo_filesystem(const std::string_view type) {
	return std::find(pseudo_filesystems.begin(), pseudo_filesystems.end(), type) != pseudo_filesystems.end();
}

std::vector<FilesystemInfo> parse_mounts(std::istream& input) {
	std::vector<FilesystemInfo> filesystems;
	std::string device;
	while (input >> device) {
		std::string mount_point;
		std::string type;
		std::string remainder;
		if (!(input >> mount_point >> type)) {
			break;
		}
		std::getline(input, remainder); // consume the rest of the line
		if (is_pseudo_filesystem(type)) {
			continue;
		}
		FilesystemInfo entry;
		entry.device	 = decode_mount_path(device);
		entry.mount_point = decode_mount_path(mount_point);
		entry.type	  = std::move(type);
		filesystems.push_back(std::move(entry));
	}
	return filesystems;
}

bool StorageManager::available() const {
#ifdef __linux__
	return true;
#else
	return false;
#endif
}

std::vector<FilesystemInfo> StorageManager::list_filesystems() const {
	if (!available()) {
		return {};
	}
	std::ifstream input("/proc/self/mounts");
	if (!input) {
		return {};
	}
	auto filesystems = parse_mounts(input);

	for (auto& filesystem : filesystems) {
		struct statvfs statistics {};
		if (statvfs(filesystem.mount_point.c_str(), &statistics) != 0) {
			filesystem.total_bytes = 0;
			continue;
		}
		filesystem.total_bytes	  = static_cast<std::uint64_t>(statistics.f_blocks) * static_cast<std::uint64_t>(statistics.f_frsize);
		filesystem.available_bytes = static_cast<std::uint64_t>(statistics.f_bavail) * static_cast<std::uint64_t>(statistics.f_frsize);
	}
	filesystems.erase(std::remove_if(filesystems.begin(), filesystems.end(),
			      [](const FilesystemInfo& filesystem) { return filesystem.total_bytes == 0; }),
	    filesystems.end());

	std::sort(filesystems.begin(), filesystems.end(), [](const FilesystemInfo& left, const FilesystemInfo& right) {
		return left.total_bytes != right.total_bytes ? left.total_bytes > right.total_bytes : left.mount_point < right.mount_point;
	});
	return filesystems;
}

} // namespace apadana
