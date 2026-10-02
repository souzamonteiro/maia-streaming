#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <fstream>
#include <cstdint>
#include "maia/core/json.hpp"
#include "maia/storage/storage.hpp"
#include "maia/media/asset.hpp"
#include "maia/media/job.hpp"

namespace maia::media {

enum class RecordingState {
    Active,
    Stopped,
    Failed
};

std::string recording_state_to_string(RecordingState s);
RecordingState recording_state_from_string(std::string_view s);

class RecordingSession {
public:
    RecordingSession(std::string id,
                     std::string room_id,
                     std::string title,
                     std::shared_ptr<storage::Storage> storage);
    ~RecordingSession();

    bool start();
    bool write_packet(const std::uint8_t* data, std::size_t size);
    bool stop();

    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& room_id() const noexcept { return room_id_; }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    [[nodiscard]] RecordingState state() const noexcept { return state_; }
    [[nodiscard]] std::uint64_t bytes_written() const noexcept { return bytes_written_; }
    [[nodiscard]] std::uint64_t packet_count() const noexcept { return packet_count_; }
    [[nodiscard]] const std::string& created_at() const noexcept { return created_at_; }
    [[nodiscard]] const std::string& finished_at() const noexcept { return finished_at_; }
    [[nodiscard]] const std::string& storage_key() const noexcept { return storage_key_; }
    [[nodiscard]] const std::string& asset_id() const noexcept { return asset_id_; }
    void set_asset_id(std::string aid) { asset_id_ = std::move(aid); }

    [[nodiscard]] core::Json to_json() const;

private:
    std::string id_;
    std::string room_id_;
    std::string title_;
    std::shared_ptr<storage::Storage> storage_;
    RecordingState state_{RecordingState::Active};
    std::string storage_key_;
    std::string asset_id_;
    std::string created_at_;
    std::string finished_at_;
    std::uint64_t bytes_written_{0};
    std::uint64_t packet_count_{0};
    std::ofstream file_stream_;
    mutable std::mutex mutex_;
};

class RecordingManager {
public:
    RecordingManager(std::shared_ptr<storage::Storage> storage,
                     std::shared_ptr<AssetRegistry> asset_registry,
                     std::shared_ptr<JobManager> job_manager);

    [[nodiscard]] std::shared_ptr<RecordingSession> start_recording(
        std::string_view room_id, std::string_view title = "");
    [[nodiscard]] std::shared_ptr<RecordingSession> get_recording(std::string_view recording_id) const;
    [[nodiscard]] std::vector<std::shared_ptr<RecordingSession>> list_recordings() const;
    bool stop_recording(std::string_view recording_id);

private:
    std::shared_ptr<storage::Storage> storage_;
    std::shared_ptr<AssetRegistry> asset_registry_;
    std::shared_ptr<JobManager> job_manager_;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<RecordingSession>> recordings_;
};

} // namespace maia::media

