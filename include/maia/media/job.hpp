#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include "maia/core/json.hpp"
#include "maia/core/thread_pool.hpp"
#include "maia/storage/storage.hpp"
#include "maia/media/asset.hpp"

namespace maia::media {

enum class JobType {
    Normalize,
    Thumbnail,
    ExtractAudio,
    HlsPackage,
    Transcode,
    Unknown
};

enum class JobState {
    Queued,
    Claimed,
    Running,
    Succeeded,
    Failed,
    Cancelled
};

std::string job_type_to_string(JobType type);
JobType job_type_from_string(std::string_view s);

std::string job_state_to_string(JobState state);
JobState job_state_from_string(std::string_view s);

struct Job {
    std::string id;
    std::string asset_id;
    JobType type = JobType::Normalize;
    JobState state = JobState::Queued;
    int progress_percent = 0;
    std::string error_message;
    std::string created_at;
    std::string started_at;
    std::string finished_at;
    core::Json parameters;
    core::Json result;

    [[nodiscard]] core::Json to_json() const;
    static Job from_json(const core::Json& j);
};

class JobManager {
public:
    JobManager(std::shared_ptr<storage::Storage> storage,
               std::shared_ptr<AssetRegistry> asset_registry,
               std::shared_ptr<core::ThreadPool> thread_pool,
               std::string ffmpeg_binary = "/usr/bin/ffmpeg",
               std::string ffprobe_binary = "/usr/bin/ffprobe");

    [[nodiscard]] std::string enqueue_job(std::string_view asset_id, JobType type, core::Json params = core::Json::object());
    [[nodiscard]] std::optional<Job> get_job(std::string_view job_id) const;
    [[nodiscard]] std::vector<Job> list_jobs_for_asset(std::string_view asset_id) const;
    [[nodiscard]] std::vector<Job> list_all_jobs() const;
    bool cancel_job(std::string_view job_id);

    [[nodiscard]] bool has_ffmpeg() const noexcept { return has_ffmpeg_; }

private:
    std::shared_ptr<storage::Storage> storage_;
    std::shared_ptr<AssetRegistry> asset_registry_;
    std::shared_ptr<core::ThreadPool> thread_pool_;
    std::string ffmpeg_path_;
    std::string ffprobe_path_;
    bool has_ffmpeg_{false};

    mutable std::mutex mutex_;
    std::map<std::string, Job> jobs_;

    void execute_job(std::string job_id);
    bool run_normalize_job(Job& job, Asset& asset);
    bool run_thumbnail_job(Job& job, Asset& asset);
    bool run_extract_audio_job(Job& job, Asset& asset);
    bool run_hls_package_job(Job& job, Asset& asset);
    bool run_transcode_job(Job& job, Asset& asset);
};

} // namespace maia::media

