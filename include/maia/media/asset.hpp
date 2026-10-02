#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <cstdint>
#include <optional>
#include <memory>
#include <mutex>
#include "maia/core/json.hpp"
#include "maia/storage/storage.hpp"

namespace maia::media {

enum class MediaType {
    Video,
    Audio,
    Image,
    Unknown
};

enum class AssetState {
    Staged,
    Ready,
    Processing,
    Deleted
};

std::string media_type_to_string(MediaType type);
MediaType media_type_from_string(std::string_view s);

std::string asset_state_to_string(AssetState state);
AssetState asset_state_from_string(std::string_view s);

struct Rendition {
    std::string id;
    std::string asset_id;
    std::string name = "source";
    std::string container = "mp4";
    std::string codec_video;
    std::string codec_audio;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t bitrate_bps = 0;
    std::uint64_t byte_size = 0;
    std::string storage_key;
    std::string checksum;

    [[nodiscard]] core::Json to_json() const;
    static Rendition from_json(const core::Json& j);
};

struct Asset {
    std::string id;
    std::string owner = "default";
    std::string title;
    MediaType media_type = MediaType::Video;
    std::string container = "mp4";
    std::uint64_t duration_ms = 0;
    std::uint64_t size_bytes = 0;
    std::string mime_type = "video/mp4";
    AssetState state = AssetState::Ready;
    std::string created_at;
    std::string storage_key;
    std::string thumbnail_key;
    std::string hls_master_key;
    std::vector<Rendition> renditions;
    std::map<std::string, std::string> metadata;

    [[nodiscard]] core::Json to_json() const;
    static Asset from_json(const core::Json& j);
};

struct ProbedMedia {
    MediaType media_type = MediaType::Unknown;
    std::string container;
    std::string mime_type = "application/octet-stream";
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t duration_ms = 0;
    std::uint64_t bitrate_bps = 0;
    std::string codec_video;
    std::string codec_audio;
};

ProbedMedia probe_media(const std::uint8_t* header, std::size_t len, std::string_view filename = "");
ProbedMedia probe_media_file(const std::filesystem::path& file_path, const std::string& ffprobe_binary = "");

class AssetRegistry {
public:
    explicit AssetRegistry(std::shared_ptr<storage::Storage> storage);

    bool init();
    bool add_asset(const Asset& asset);
    bool update_asset(const Asset& asset);
    [[nodiscard]] std::optional<Asset> get_asset(std::string_view asset_id) const;
    [[nodiscard]] std::vector<Asset> list_assets(std::string_view owner = "") const;
    bool delete_asset(std::string_view asset_id, bool remove_files = true);

private:
    std::shared_ptr<storage::Storage> storage_;
    mutable std::mutex mutex_;
    std::map<std::string, Asset> assets_;

    bool save_asset_metadata(const Asset& asset);
};

} // namespace maia::media

