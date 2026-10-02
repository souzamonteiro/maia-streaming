#include "maia/media/asset.hpp"
#include "maia/core/utils.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

namespace maia::media {

std::string media_type_to_string(MediaType type) {
    switch (type) {
        case MediaType::Video: return "video";
        case MediaType::Audio: return "audio";
        case MediaType::Image: return "image";
        case MediaType::Unknown: return "unknown";
    }
    return "unknown";
}

MediaType media_type_from_string(std::string_view s) {
    if (s == "video") return MediaType::Video;
    if (s == "audio") return MediaType::Audio;
    if (s == "image") return MediaType::Image;
    return MediaType::Unknown;
}

std::string asset_state_to_string(AssetState state) {
    switch (state) {
        case AssetState::Staged: return "staged";
        case AssetState::Ready: return "ready";
        case AssetState::Processing: return "processing";
        case AssetState::Deleted: return "deleted";
    }
    return "ready";
}

AssetState asset_state_from_string(std::string_view s) {
    if (s == "staged") return AssetState::Staged;
    if (s == "processing") return AssetState::Processing;
    if (s == "deleted") return AssetState::Deleted;
    return AssetState::Ready;
}

core::Json Rendition::to_json() const {
    core::Json j = core::Json::object();
    j["id"] = id;
    j["asset_id"] = asset_id;
    j["name"] = name;
    j["container"] = container;
    j["codec_video"] = codec_video;
    j["codec_audio"] = codec_audio;
    j["width"] = width;
    j["height"] = height;
    j["bitrate_bps"] = bitrate_bps;
    j["byte_size"] = byte_size;
    j["storage_key"] = storage_key;
    j["checksum"] = checksum;
    return j;
}

Rendition Rendition::from_json(const core::Json& j) {
    Rendition r;
    r.id = j.get("id").as_string();
    r.asset_id = j.get("asset_id").as_string();
    r.name = j.get("name").as_string("source");
    r.container = j.get("container").as_string("mp4");
    r.codec_video = j.get("codec_video").as_string();
    r.codec_audio = j.get("codec_audio").as_string();
    r.width = static_cast<std::uint32_t>(j.get("width").as_uint64());
    r.height = static_cast<std::uint32_t>(j.get("height").as_uint64());
    r.bitrate_bps = j.get("bitrate_bps").as_uint64();
    r.byte_size = j.get("byte_size").as_uint64();
    r.storage_key = j.get("storage_key").as_string();
    r.checksum = j.get("checksum").as_string();
    return r;
}

core::Json Asset::to_json() const {
    core::Json j = core::Json::object();
    j["id"] = id;
    j["owner"] = owner;
    j["title"] = title;
    j["media_type"] = media_type_to_string(media_type);
    j["container"] = container;
    j["duration_ms"] = duration_ms;
    j["size_bytes"] = size_bytes;
    j["mime_type"] = mime_type;
    j["state"] = asset_state_to_string(state);
    j["created_at"] = created_at;
    j["storage_key"] = storage_key;
    j["thumbnail_key"] = thumbnail_key;
    j["hls_master_key"] = hls_master_key;

    core::Json r_arr = core::Json::array();
    for (const auto& r : renditions) {
        r_arr.push_back(r.to_json());
    }
    j["renditions"] = std::move(r_arr);

    core::Json m_obj = core::Json::object();
    for (const auto& [k, v] : metadata) {
        m_obj[k] = v;
    }
    j["metadata"] = std::move(m_obj);

    return j;
}

Asset Asset::from_json(const core::Json& j) {
    Asset a;
    a.id = j.get("id").as_string();
    a.owner = j.get("owner").as_string("default");
    a.title = j.get("title").as_string();
    a.media_type = media_type_from_string(j.get("media_type").as_string("video"));
    a.container = j.get("container").as_string("mp4");
    a.duration_ms = j.get("duration_ms").as_uint64();
    a.size_bytes = j.get("size_bytes").as_uint64();
    a.mime_type = j.get("mime_type").as_string("video/mp4");
    a.state = asset_state_from_string(j.get("state").as_string("ready"));
    a.created_at = j.get("created_at").as_string();
    a.storage_key = j.get("storage_key").as_string();
    a.thumbnail_key = j.get("thumbnail_key").as_string();
    a.hls_master_key = j.get("hls_master_key").as_string();

    auto r_arr = j.get("renditions").as_array();
    for (const auto& r_j : r_arr) {
        a.renditions.push_back(Rendition::from_json(r_j));
    }

    auto m_obj = j.get("metadata").as_object();
    for (const auto& [k, v] : m_obj) {
        a.metadata[k] = v.as_string();
    }

    return a;
}

ProbedMedia probe_media(const std::uint8_t* header, std::size_t len, std::string_view filename) {
    ProbedMedia p;
    std::string ext;
    auto dot = filename.find_last_of('.');
    if (dot != std::string_view::npos) {
        ext = core::to_lower(filename.substr(dot));
    }

    if (len >= 8 && header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p') {
        p.media_type = MediaType::Video;
        p.container = "mp4";
        p.mime_type = "video/mp4";
        p.codec_video = "h264";
        p.codec_audio = "aac";
        return p;
    }

    if (len >= 4 && header[0] == 0x1A && header[1] == 0x45 && header[2] == 0xDF && header[3] == 0xA3) {
        p.media_type = MediaType::Video;
        p.container = "webm";
        p.mime_type = "video/webm";
        p.codec_video = "vp8";
        p.codec_audio = "opus";
        return p;
    }

    if (len >= 12 && header[0] == 'R' && header[1] == 'I' && header[2] == 'F' && header[3] == 'F' &&
        header[8] == 'W' && header[9] == 'A' && header[10] == 'V' && header[11] == 'E') {
        p.media_type = MediaType::Audio;
        p.container = "wav";
        p.mime_type = "audio/wav";
        p.codec_audio = "pcm";
        return p;
    }

    if (len >= 4 && header[0] == 'O' && header[1] == 'g' && header[2] == 'g' && header[3] == 'S') {
        p.media_type = MediaType::Audio;
        p.container = "ogg";
        p.mime_type = "audio/ogg";
        p.codec_audio = "vorbis";
        return p;
    }

    if (len >= 4 && header[0] == 'f' && header[1] == 'L' && header[2] == 'a' && header[3] == 'C') {
        p.media_type = MediaType::Audio;
        p.container = "flac";
        p.mime_type = "audio/flac";
        p.codec_audio = "flac";
        return p;
    }

    if ((len >= 3 && header[0] == 'I' && header[1] == 'D' && header[2] == '3') ||
        (len >= 2 && header[0] == 0xFF && (header[1] & 0xE0) == 0xE0)) {
        p.media_type = MediaType::Audio;
        p.container = "mp3";
        p.mime_type = "audio/mpeg";
        p.codec_audio = "mp3";
        return p;
    }

    if (len >= 4 && header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G') {
        p.media_type = MediaType::Image;
        p.container = "png";
        p.mime_type = "image/png";
        return p;
    }

    if (len >= 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) {
        p.media_type = MediaType::Image;
        p.container = "jpg";
        p.mime_type = "image/jpeg";
        return p;
    }

    // Fallback based on extension
    if (ext == ".mp4" || ext == ".m4v") {
        p.media_type = MediaType::Video;
        p.container = "mp4";
        p.mime_type = "video/mp4";
    } else if (ext == ".webm") {
        p.media_type = MediaType::Video;
        p.container = "webm";
        p.mime_type = "video/webm";
    } else if (ext == ".mp3") {
        p.media_type = MediaType::Audio;
        p.container = "mp3";
        p.mime_type = "audio/mpeg";
    } else if (ext == ".wav") {
        p.media_type = MediaType::Audio;
        p.container = "wav";
        p.mime_type = "audio/wav";
    } else if (ext == ".ogg") {
        p.media_type = MediaType::Audio;
        p.container = "ogg";
        p.mime_type = "audio/ogg";
    } else if (ext == ".m3u8") {
        p.media_type = MediaType::Video;
        p.container = "hls";
        p.mime_type = "application/vnd.apple.mpegurl";
    } else if (ext == ".ts") {
        p.media_type = MediaType::Video;
        p.container = "ts";
        p.mime_type = "video/MP2T";
    } else {
        p.media_type = MediaType::Unknown;
        p.container = ext.empty() ? "bin" : ext.substr(1);
        p.mime_type = core::mime_type_from_filename(filename);
    }

    return p;
}

ProbedMedia probe_media_file(const std::filesystem::path& file_path, const std::string& /*ffprobe_binary*/) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        ProbedMedia p;
        return p;
    }

    std::uint8_t buffer[64];
    file.read(reinterpret_cast<char*>(buffer), sizeof(buffer));
    std::size_t bytes = static_cast<std::size_t>(file.gcount());

    auto p = probe_media(buffer, bytes, file_path.filename().string());

    // If duration or dimensions not known, set sane defaults for playback
    if (p.media_type == MediaType::Video) {
        if (p.width == 0) p.width = 1280;
        if (p.height == 0) p.height = 720;
    }
    return p;
}

AssetRegistry::AssetRegistry(std::shared_ptr<storage::Storage> storage)
    : storage_(std::move(storage)) {}

bool AssetRegistry::init() {
    std::lock_guard<std::mutex> lock(mutex_);
    assets_.clear();

    if (!storage_) return false;

    // Scan assets directory for .meta.json files
    auto files = storage_->list(storage::Category::Assets);
    for (const auto& file_info : files) {
        if (file_info.key.ends_with(".meta.json")) {
            auto chunk = storage_->read_chunk(storage::Category::Assets, file_info.key, 0, file_info.size);
            if (chunk) {
                std::string json_str(chunk->begin(), chunk->end());
                auto j = core::Json::parse(json_str);
                if (j && j->is_object()) {
                    auto asset = Asset::from_json(*j);
                    if (!asset.id.empty()) {
                        assets_[asset.id] = std::move(asset);
                    }
                }
            }
        }
    }
    return true;
}

bool AssetRegistry::save_asset_metadata(const Asset& asset) {
    if (!storage_) return false;
    std::string meta_key = asset.id + ".meta.json";
    std::string json_str = asset.to_json().dump(2);
    return storage_->write_file(storage::Category::Assets, meta_key, json_str);
}

bool AssetRegistry::add_asset(const Asset& asset) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        assets_[asset.id] = asset;
    }
    return save_asset_metadata(asset);
}

bool AssetRegistry::update_asset(const Asset& asset) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        assets_[asset.id] = asset;
    }
    return save_asset_metadata(asset);
}

std::optional<Asset> AssetRegistry::get_asset(std::string_view asset_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = assets_.find(std::string(asset_id));
    if (it != assets_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Asset> AssetRegistry::list_assets(std::string_view owner) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Asset> list;
    list.reserve(assets_.size());
    for (const auto& [id, asset] : assets_) {
        if (owner.empty() || asset.owner == owner) {
            list.push_back(asset);
        }
    }
    return list;
}

bool AssetRegistry::delete_asset(std::string_view asset_id, bool remove_files) {
    Asset asset_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = assets_.find(std::string(asset_id));
        if (it == assets_.end()) return false;
        asset_copy = it->second;
        assets_.erase(it);
    }

    if (storage_) {
        // Remove metadata
        storage_->delete_file(storage::Category::Assets, std::string(asset_id) + ".meta.json");

        if (remove_files) {
            if (!asset_copy.storage_key.empty()) {
                storage_->delete_file(storage::Category::Assets, asset_copy.storage_key);
            }
            if (!asset_copy.thumbnail_key.empty()) {
                storage_->delete_file(storage::Category::Thumbnails, asset_copy.thumbnail_key);
            }
            for (const auto& r : asset_copy.renditions) {
                if (!r.storage_key.empty()) {
                    storage_->delete_file(storage::Category::Assets, r.storage_key);
                }
            }
        }
    }
    return true;
}

} // namespace maia::media

