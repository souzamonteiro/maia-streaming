#include "maia/media/asset.hpp"
#include "maia/storage/storage.hpp"
#include <cassert>
#include <iostream>
#include <filesystem>

int main() {
    using maia::media::probe_media;
    using maia::media::Asset;
    using maia::media::AssetRegistry;
    using maia::media::MediaType;
    using maia::media::AssetState;
    using maia::storage::FilesystemStorage;

    // 1. Media probe validation with magic bytes
    std::string mp4_magic = "\x00\x00\x00\x18\x66\x74\x79\x70\x69\x73\x6f\x6d";
    auto p1 = probe_media(reinterpret_cast<const uint8_t*>(mp4_magic.data()), mp4_magic.size(), "test.mp4");
    assert(p1.container == "mp4");
    assert(p1.mime_type == "video/mp4");
    assert(p1.media_type == MediaType::Video);

    std::string webm_magic = "\x1a\x45\xdf\xa3\x9f\x42\x86\x81\x01\x42\xf7\x81\x01";
    auto p2 = probe_media(reinterpret_cast<const uint8_t*>(webm_magic.data()), webm_magic.size(), "clip.webm");
    assert(p2.container == "webm");
    assert(p2.mime_type == "video/webm");
    assert(p2.media_type == MediaType::Video);

    std::string wav_magic = "RIFF\x24\x00\x00\x00WAVEfmt \x10\x00\x00\x00";
    auto p3 = probe_media(reinterpret_cast<const uint8_t*>(wav_magic.data()), wav_magic.size(), "audio.wav");
    assert(p3.container == "wav");
    assert(p3.mime_type == "audio/wav");
    assert(p3.media_type == MediaType::Audio);

    // 2. Asset JSON serialization & deserialization round-trip
    Asset asset;
    asset.id = "asset-test-01";
    asset.owner = "admin";
    asset.title = "Big Buck Bunny 4K";
    asset.media_type = MediaType::Video;
    asset.container = "mp4";
    asset.duration_ms = 600000;
    asset.size_bytes = 600000000;
    asset.mime_type = "video/mp4";
    asset.state = AssetState::Ready;
    asset.created_at = "2026-10-02T12:00:00Z";
    asset.storage_key = "asset-test-01.mp4";

    auto json = asset.to_json();
    assert(json.is_object());
    assert(json["id"].as_string() == "asset-test-01");

    auto restored = Asset::from_json(json);
    assert(restored.id == asset.id);
    assert(restored.title == asset.title);
    assert(restored.owner == asset.owner);
    assert(restored.storage_key == asset.storage_key);
    assert(restored.duration_ms == 600000);
    assert(restored.mime_type == "video/mp4");
    assert(restored.state == AssetState::Ready);

    // 3. AssetRegistry operations backed by storage
    std::string test_dir = "/tmp/maia_asset_registry_test";
    std::filesystem::remove_all(test_dir);
    auto storage = std::make_shared<FilesystemStorage>(test_dir);
    assert(storage->is_accessible());

    AssetRegistry registry(storage);
    assert(registry.init());
    assert(registry.list_assets().empty());
    assert(registry.add_asset(asset));
    assert(registry.list_assets().size() == 1);

    auto retrieved_opt = registry.get_asset("asset-test-01");
    assert(retrieved_opt.has_value());
    assert(retrieved_opt->title == "Big Buck Bunny 4K");

    auto all_assets = registry.list_assets();
    assert(all_assets.size() == 1);
    assert(all_assets[0].id == "asset-test-01");

    // Remove asset
    assert(registry.delete_asset("asset-test-01", false));
    assert(registry.list_assets().empty());
    assert(!registry.get_asset("asset-test-01").has_value());

    std::filesystem::remove_all(test_dir);
    std::cout << "[Test PASS] Asset probe, JSON serialization and registry validation\n";
    return 0;
}
