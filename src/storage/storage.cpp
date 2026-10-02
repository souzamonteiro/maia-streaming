#include "maia/storage/storage.hpp"
#include "maia/core/utils.hpp"
#include <system_error>
#include <iostream>
#include <sstream>

namespace maia::storage {

namespace {
std::string category_name(Category cat) {
    switch (cat) {
        case Category::Assets: return "assets";
        case Category::Hls: return "hls";
        case Category::Thumbnails: return "thumbnails";
        case Category::Recordings: return "recordings";
        case Category::Staging: return "staging";
    }
    return "misc";
}
}

FilesystemStorage::FilesystemStorage(std::string root_path) {
    std::error_code ec;
    std::filesystem::path p(root_path);

    // Try to create root and categories
    if (!std::filesystem::exists(p, ec)) {
        std::filesystem::create_directories(p, ec);
    }

    if (ec) {
        // Fallback to local ./data/media if system directory cannot be created
        p = std::filesystem::current_path() / "data" / "media";
        ec.clear();
        std::filesystem::create_directories(p, ec);
    }

    root_ = std::filesystem::weakly_canonical(p, ec);
    if (ec) root_ = p;

    // Ensure all subdirectories exist
    for (auto cat : {Category::Assets, Category::Hls, Category::Thumbnails, Category::Recordings, Category::Staging}) {
        std::filesystem::create_directories(category_dir(cat), ec);
    }
}

std::filesystem::path FilesystemStorage::category_dir(Category cat) const {
    return root_ / category_name(cat);
}

std::optional<std::filesystem::path> FilesystemStorage::resolve_safe_path(Category cat, std::string_view key) const {
    if (key.empty()) return std::nullopt;

    // Reject null bytes and path traversal sequences
    if (key.find('\0') != std::string_view::npos) return std::nullopt;
    if (key.find("..") != std::string_view::npos) return std::nullopt;
    if (key.starts_with('/') || key.starts_with('\\')) return std::nullopt;

    auto cat_path = category_dir(cat);
    auto target = cat_path / std::filesystem::path(std::string(key));

    std::error_code ec;
    auto canon_cat = std::filesystem::weakly_canonical(cat_path, ec);
    auto canon_target = std::filesystem::weakly_canonical(target, ec);

    // Verify canonical path is strictly inside the category directory
    std::string cat_str = canon_cat.string();
    std::string target_str = canon_target.string();

    if (!cat_str.empty() && cat_str.back() != '/') {
        cat_str.push_back('/');
    }

    if (!target_str.starts_with(cat_str) && target_str != canon_cat.string()) {
        return std::nullopt;
    }

    return target;
}

bool FilesystemStorage::is_accessible() const {
    std::error_code ec;
    return std::filesystem::exists(root_, ec) && std::filesystem::is_directory(root_, ec);
}

bool FilesystemStorage::exists(Category cat, std::string_view key) const {
    auto path = resolve_safe_path(cat, key);
    if (!path) return false;
    std::error_code ec;
    return std::filesystem::exists(*path, ec) && std::filesystem::is_regular_file(*path, ec);
}

std::optional<FileInfo> FilesystemStorage::stat(Category cat, std::string_view key) const {
    auto path = resolve_safe_path(cat, key);
    if (!path) return std::nullopt;

    std::error_code ec;
    if (!std::filesystem::exists(*path, ec) || !std::filesystem::is_regular_file(*path, ec)) {
        return std::nullopt;
    }

    auto size = std::filesystem::file_size(*path, ec);
    if (ec) return std::nullopt;

    auto ftime = std::filesystem::last_write_time(*path, ec);
    if (ec) return std::nullopt;

    auto s_time = std::chrono::time_point_cast<std::chrono::seconds>(
        decltype(ftime)::clock::to_sys(ftime));
    auto epoch_sec = static_cast<std::uint64_t>(s_time.time_since_epoch().count());

    // Compute strong ETag based on size and mtime
    std::ostringstream ss;
    ss << '"' << std::hex << size << '-' << epoch_sec << '"';

    FileInfo info;
    info.key = std::string(key);
    info.size = size;
    info.last_modified_epoch = epoch_sec;
    info.etag = ss.str();
    return info;
}

std::optional<std::vector<std::uint8_t>> FilesystemStorage::read_chunk(
    Category cat, std::string_view key, std::uint64_t offset, std::size_t length) const {
    auto path = resolve_safe_path(cat, key);
    if (!path) return std::nullopt;

    std::ifstream file(*path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;

    file.seekg(0, std::ios::end);
    auto total_size = static_cast<std::uint64_t>(file.tellg());
    if (offset >= total_size) {
        return std::nullopt;
    }

    std::size_t to_read = length;
    if (offset + to_read > total_size) {
        to_read = static_cast<std::size_t>(total_size - offset);
    }

    std::vector<std::uint8_t> buffer(to_read);
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(to_read));
    std::size_t bytes_read = static_cast<std::size_t>(file.gcount());
    buffer.resize(bytes_read);
    return buffer;
}

bool FilesystemStorage::write_file(Category cat, std::string_view key, const std::uint8_t* data, std::size_t length) {
    auto path = resolve_safe_path(cat, key);
    if (!path) return false;

    std::error_code ec;
    std::filesystem::create_directories(path->parent_path(), ec);

    std::ofstream file(*path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    if (length > 0 && data != nullptr) {
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
    }
    return file.good();
}

bool FilesystemStorage::write_file(Category cat, std::string_view key, std::string_view data) {
    return write_file(cat, key, reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
}

bool FilesystemStorage::delete_file(Category cat, std::string_view key) {
    auto path = resolve_safe_path(cat, key);
    if (!path) return false;
    std::error_code ec;
    return std::filesystem::remove(*path, ec);
}

std::optional<std::filesystem::path> FilesystemStorage::get_path(Category cat, std::string_view key) const {
    return resolve_safe_path(cat, key);
}

std::vector<FileInfo> FilesystemStorage::list(Category cat) const {
    std::vector<FileInfo> result;
    auto cat_path = category_dir(cat);
    std::error_code ec;
    if (!std::filesystem::exists(cat_path, ec)) return result;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(cat_path, ec)) {
        if (entry.is_regular_file(ec)) {
            auto rel = std::filesystem::relative(entry.path(), cat_path, ec);
            if (!ec) {
                auto st = stat(cat, rel.string());
                if (st) result.push_back(*st);
            }
        }
    }
    return result;
}

} // namespace maia::storage

