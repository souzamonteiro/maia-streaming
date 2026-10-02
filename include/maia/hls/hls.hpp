#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <memory>
#include "maia/storage/storage.hpp"
#include "maia/media/asset.hpp"

namespace maia::hls {

struct HlsSegment {
    std::string filename;
    double duration_seconds = 10.0;
    std::uint64_t size_bytes = 0;
};

struct HlsPlaylist {
    std::string rendition_name;
    double target_duration = 10.0;
    std::vector<HlsSegment> segments;
    bool is_vod = true;

    [[nodiscard]] std::string serialize() const;
};

class HlsManager {
public:
    explicit HlsManager(std::shared_ptr<storage::Storage> storage,
                        std::shared_ptr<media::AssetRegistry> asset_registry);

    [[nodiscard]] std::optional<std::string> get_master_playlist(std::string_view asset_id) const;
    [[nodiscard]] std::optional<std::string> get_media_playlist(std::string_view asset_id, std::string_view rendition) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_segment(
        std::string_view asset_id, std::string_view rendition, std::string_view segment_filename) const;

    bool package_simple_hls(const media::Asset& asset);

private:
    std::shared_ptr<storage::Storage> storage_;
    std::shared_ptr<media::AssetRegistry> asset_registry_;
};

} // namespace maia::hls

