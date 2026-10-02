#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <optional>
#include <filesystem>
#include <fstream>

namespace maia::storage {

enum class Category {
    Assets,
    Hls,
    Thumbnails,
    Recordings,
    Staging
};

struct FileInfo {
    std::string key;
    std::uint64_t size = 0;
    std::uint64_t last_modified_epoch = 0;
    std::string etag;
};

class Storage {
public:
    virtual ~Storage() = default;

    [[nodiscard]] virtual bool exists(Category cat, std::string_view key) const = 0;
    [[nodiscard]] virtual std::optional<FileInfo> stat(Category cat, std::string_view key) const = 0;
    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>> read_chunk(
        Category cat, std::string_view key, std::uint64_t offset, std::size_t length) const = 0;
    virtual bool write_file(Category cat, std::string_view key, const std::uint8_t* data, std::size_t length) = 0;
    virtual bool write_file(Category cat, std::string_view key, std::string_view data) = 0;
    virtual bool delete_file(Category cat, std::string_view key) = 0;
    [[nodiscard]] virtual std::optional<std::filesystem::path> get_path(Category cat, std::string_view key) const = 0;
    [[nodiscard]] virtual std::vector<FileInfo> list(Category cat) const = 0;
    [[nodiscard]] virtual bool is_accessible() const = 0;
};

class FilesystemStorage : public Storage {
public:
    explicit FilesystemStorage(std::string root_path);

    [[nodiscard]] bool exists(Category cat, std::string_view key) const override;
    [[nodiscard]] std::optional<FileInfo> stat(Category cat, std::string_view key) const override;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_chunk(
        Category cat, std::string_view key, std::uint64_t offset, std::size_t length) const override;
    bool write_file(Category cat, std::string_view key, const std::uint8_t* data, std::size_t length) override;
    bool write_file(Category cat, std::string_view key, std::string_view data) override;
    bool delete_file(Category cat, std::string_view key) override;
    [[nodiscard]] std::optional<std::filesystem::path> get_path(Category cat, std::string_view key) const override;
    [[nodiscard]] std::vector<FileInfo> list(Category cat) const override;
    [[nodiscard]] bool is_accessible() const override;

    [[nodiscard]] const std::filesystem::path& root_path() const noexcept { return root_; }

private:
    std::filesystem::path root_;
    [[nodiscard]] std::optional<std::filesystem::path> resolve_safe_path(Category cat, std::string_view key) const;
    [[nodiscard]] std::filesystem::path category_dir(Category cat) const;
};

} // namespace maia::storage

