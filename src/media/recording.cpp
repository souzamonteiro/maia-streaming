#include "maia/media/recording.hpp"
#include "maia/core/utils.hpp"
#include <iostream>

namespace maia::media {

std::string recording_state_to_string(RecordingState s) {
    switch (s) {
        case RecordingState::Active: return "active";
        case RecordingState::Stopped: return "stopped";
        case RecordingState::Failed: return "failed";
    }
    return "active";
}

RecordingState recording_state_from_string(std::string_view s) {
    if (s == "stopped") return RecordingState::Stopped;
    if (s == "failed") return RecordingState::Failed;
    return RecordingState::Active;
}

RecordingSession::RecordingSession(std::string id,
                                   std::string room_id,
                                   std::string title,
                                   std::shared_ptr<storage::Storage> storage)
    : id_(std::move(id)),
      room_id_(std::move(room_id)),
      title_(std::move(title)),
      storage_(std::move(storage)),
      created_at_(core::iso8601_now()) {
    storage_key_ = id_ + ".rec";
}

RecordingSession::~RecordingSession() {
    stop();
}

bool RecordingSession::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!storage_) return false;
    auto path_opt = storage_->get_path(storage::Category::Recordings, storage_key_);
    if (!path_opt) return false;

    std::error_code ec;
    std::filesystem::create_directories(path_opt->parent_path(), ec);

    file_stream_.open(*path_opt, std::ios::binary | std::ios::trunc);
    if (!file_stream_.is_open()) {
        state_ = RecordingState::Failed;
        return false;
    }

    state_ = RecordingState::Active;
    return true;
}

bool RecordingSession::write_packet(const std::uint8_t* data, std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != RecordingState::Active || !file_stream_.is_open()) {
        return false;
    }

    // Write simple frame header: 4-byte size + payload
    uint32_t net_size = static_cast<uint32_t>(size);
    file_stream_.write(reinterpret_cast<const char*>(&net_size), sizeof(net_size));
    file_stream_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));

    bytes_written_ += (sizeof(net_size) + size);
    packet_count_++;
    return file_stream_.good();
}

bool RecordingSession::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != RecordingState::Active) return true;

    if (file_stream_.is_open()) {
        file_stream_.flush();
        file_stream_.close();
    }

    state_ = RecordingState::Stopped;
    finished_at_ = core::iso8601_now();
    return true;
}

core::Json RecordingSession::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    core::Json j = core::Json::object();
    j["id"] = id_;
    j["room_id"] = room_id_;
    j["title"] = title_;
    j["state"] = recording_state_to_string(state_);
    j["bytes_written"] = bytes_written_;
    j["packet_count"] = packet_count_;
    j["created_at"] = created_at_;
    j["finished_at"] = finished_at_;
    j["storage_key"] = storage_key_;
    j["asset_id"] = asset_id_;
    return j;
}

RecordingManager::RecordingManager(std::shared_ptr<storage::Storage> storage,
                                   std::shared_ptr<AssetRegistry> asset_registry,
                                   std::shared_ptr<JobManager> job_manager)
    : storage_(std::move(storage)),
      asset_registry_(std::move(asset_registry)),
      job_manager_(std::move(job_manager)) {}

std::shared_ptr<RecordingSession> RecordingManager::start_recording(
    std::string_view room_id, std::string_view title) {
    std::string id = core::generate_id("rec");
    std::string rec_title = title.empty() ? ("Recording of " + std::string(room_id)) : std::string(title);

    auto session = std::make_shared<RecordingSession>(id, std::string(room_id), rec_title, storage_);
    if (!session->start()) {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        recordings_[id] = session;
    }

    return session;
}

std::shared_ptr<RecordingSession> RecordingManager::get_recording(std::string_view recording_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = recordings_.find(std::string(recording_id));
    if (it != recordings_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<RecordingSession>> RecordingManager::list_recordings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<RecordingSession>> list;
    list.reserve(recordings_.size());
    for (const auto& [id, s] : recordings_) {
        list.push_back(s);
    }
    return list;
}

bool RecordingManager::stop_recording(std::string_view recording_id) {
    std::shared_ptr<RecordingSession> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = recordings_.find(std::string(recording_id));
        if (it == recordings_.end()) return false;
        session = it->second;
    }

    if (!session->stop()) {
        return false;
    }

    // Convert recording into an Asset
    Asset asset;
    asset.id = core::generate_id("01J");
    asset.owner = "meet";
    asset.title = session->title();
    asset.media_type = MediaType::Video;
    asset.container = "mp4";
    asset.size_bytes = session->bytes_written();
    asset.mime_type = "video/mp4";
    asset.state = AssetState::Staged;
    asset.created_at = core::iso8601_now();
    asset.storage_key = asset.id + ".mp4";

    // Copy/move recording file to assets category
    if (storage_ && storage_->exists(storage::Category::Recordings, session->storage_key())) {
        auto data = storage_->read_chunk(storage::Category::Recordings, session->storage_key(), 0, session->bytes_written());
        if (data) {
            storage_->write_file(storage::Category::Assets, asset.storage_key, data->data(), data->size());
        }
    }

    if (asset_registry_) {
        asset_registry_->add_asset(asset);
    }

    session->set_asset_id(asset.id);

    // Queue normalization and thumbnail jobs
    if (job_manager_) {
        (void)job_manager_->enqueue_job(asset.id, JobType::Normalize);
        (void)job_manager_->enqueue_job(asset.id, JobType::Thumbnail);
    }

    return true;
}

} // namespace maia::media

