#include "maia/hls/hls.hpp"
#include <sstream>
#include <iomanip>

namespace maia::hls {

std::string HlsPlaylist::serialize() const {
    std::ostringstream ss;
    ss << "#EXTM3U\n";
    ss << "#EXT-X-VERSION:3\n";
    ss << "#EXT-X-TARGETDURATION:" << static_cast<int>(target_duration) << "\n";
    ss << "#EXT-X-MEDIA-SEQUENCE:0\n";
    if (is_vod) {
        ss << "#EXT-X-PLAYLIST-TYPE:VOD\n";
    }

    for (const auto& seg : segments) {
        ss << std::fixed << std::setprecision(3);
        ss << "#EXTINF:" << seg.duration_seconds << ",\n";
        ss << seg.filename << "\n";
    }

    if (is_vod) {
        ss << "#EXT-X-ENDLIST\n";
    }
    return ss.str();
}

HlsManager::HlsManager(std::shared_ptr<storage::Storage> storage,
                       std::shared_ptr<media::AssetRegistry> asset_registry)
    : storage_(std::move(storage)), asset_registry_(std::move(asset_registry)) {}

std::optional<std::string> HlsManager::get_master_playlist(std::string_view asset_id) const {
    if (!storage_) return std::nullopt;

    std::string master_key = std::string(asset_id) + "/master.m3u8";
    if (storage_->exists(storage::Category::Hls, master_key)) {
        auto stat = storage_->stat(storage::Category::Hls, master_key);
        if (stat) {
            auto chunk = storage_->read_chunk(storage::Category::Hls, master_key, 0, stat->size);
            if (chunk) {
                return std::string(chunk->begin(), chunk->end());
            }
        }
    }

    // Dynamic generation if asset exists
    auto asset_opt = asset_registry_->get_asset(asset_id);
    if (!asset_opt) return std::nullopt;

    std::ostringstream ss;
    ss << "#EXTM3U\n";
    ss << "#EXT-X-VERSION:3\n";

    if (asset_opt->renditions.empty()) {
        ss << "#EXT-X-STREAM-INF:BANDWIDTH=1500000,NAME=\"source\"\n";
        ss << "source/index.m3u8\n";
    } else {
        for (const auto& r : asset_opt->renditions) {
            uint64_t bw = r.bitrate_bps > 0 ? r.bitrate_bps : 1500000;
            ss << "#EXT-X-STREAM-INF:BANDWIDTH=" << bw;
            if (r.width > 0 && r.height > 0) {
                ss << ",RESOLUTION=" << r.width << "x" << r.height;
            }
            ss << ",NAME=\"" << r.name << "\"\n";
            ss << r.name << "/index.m3u8\n";
        }
    }

    return ss.str();
}

std::optional<std::string> HlsManager::get_media_playlist(std::string_view asset_id, std::string_view rendition) const {
    if (!storage_) return std::nullopt;

    std::string key = std::string(asset_id) + "/" + std::string(rendition) + "/index.m3u8";
    if (storage_->exists(storage::Category::Hls, key)) {
        auto stat = storage_->stat(storage::Category::Hls, key);
        if (stat) {
            auto chunk = storage_->read_chunk(storage::Category::Hls, key, 0, stat->size);
            if (chunk) {
                return std::string(chunk->begin(), chunk->end());
            }
        }
    }

    // Dynamic fallback if asset exists
    auto asset_opt = asset_registry_->get_asset(asset_id);
    if (!asset_opt) return std::nullopt;

    HlsPlaylist pl;
    pl.rendition_name = std::string(rendition);
    pl.target_duration = 10.0;
    pl.is_vod = true;

    HlsSegment seg;
    seg.filename = "segment0.ts";
    seg.duration_seconds = asset_opt->duration_ms > 0 ? (asset_opt->duration_ms / 1000.0) : 10.0;
    seg.size_bytes = asset_opt->size_bytes;
    pl.segments.push_back(seg);

    return pl.serialize();
}

std::optional<std::vector<std::uint8_t>> HlsManager::get_segment(
    std::string_view asset_id, std::string_view rendition, std::string_view segment_filename) const {
    if (!storage_) return std::nullopt;

    std::string key = std::string(asset_id) + "/" + std::string(rendition) + "/" + std::string(segment_filename);
    if (storage_->exists(storage::Category::Hls, key)) {
        auto stat = storage_->stat(storage::Category::Hls, key);
        if (stat) {
            return storage_->read_chunk(storage::Category::Hls, key, 0, stat->size);
        }
    }

    // Fallback: if segment0.ts requested, deliver asset content
    if (segment_filename == "segment0.ts") {
        auto asset_opt = asset_registry_->get_asset(asset_id);
        if (asset_opt && storage_->exists(storage::Category::Assets, asset_opt->storage_key)) {
            return storage_->read_chunk(storage::Category::Assets, asset_opt->storage_key, 0, asset_opt->size_bytes);
        }
    }

    return std::nullopt;
}

bool HlsManager::package_simple_hls(const media::Asset& asset) {
    if (!storage_ || !storage_->exists(storage::Category::Assets, asset.storage_key)) {
        return false;
    }

    std::string master_key = asset.id + "/master.m3u8";
    std::string rendition_key = asset.id + "/source/index.m3u8";
    std::string segment_key = asset.id + "/source/segment0.ts";

    auto data = storage_->read_chunk(storage::Category::Assets, asset.storage_key, 0, asset.size_bytes);
    if (data) {
        storage_->write_file(storage::Category::Hls, segment_key, data->data(), data->size());
    }

    std::ostringstream media_pl;
    media_pl << "#EXTM3U\n";
    media_pl << "#EXT-X-VERSION:3\n";
    media_pl << "#EXT-X-TARGETDURATION:10\n";
    media_pl << "#EXT-X-MEDIA-SEQUENCE:0\n";
    media_pl << "#EXT-X-PLAYLIST-TYPE:VOD\n";
    media_pl << "#EXTINF:10.000,\n";
    media_pl << "segment0.ts\n";
    media_pl << "#EXT-X-ENDLIST\n";
    storage_->write_file(storage::Category::Hls, rendition_key, media_pl.str());

    std::ostringstream master_pl;
    master_pl << "#EXTM3U\n";
    master_pl << "#EXT-X-VERSION:3\n";
    master_pl << "#EXT-X-STREAM-INF:BANDWIDTH=1500000,NAME=\"source\"\n";
    master_pl << "source/index.m3u8\n";
    storage_->write_file(storage::Category::Hls, master_key, master_pl.str());

    return true;
}

} // namespace maia::hls

