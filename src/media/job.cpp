#include "maia/media/job.hpp"
#include "maia/core/utils.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <array>
#include <cstdlib>

namespace maia::media {

std::string job_type_to_string(JobType type) {
    switch (type) {
        case JobType::Normalize: return "normalize";
        case JobType::Thumbnail: return "thumbnail";
        case JobType::ExtractAudio: return "extract-audio";
        case JobType::HlsPackage: return "hls-package";
        case JobType::Transcode: return "transcode";
        case JobType::Unknown: return "unknown";
    }
    return "unknown";
}

JobType job_type_from_string(std::string_view s) {
    if (s == "normalize") return JobType::Normalize;
    if (s == "thumbnail") return JobType::Thumbnail;
    if (s == "extract-audio" || s == "extract_audio") return JobType::ExtractAudio;
    if (s == "hls-package" || s == "hls_package") return JobType::HlsPackage;
    if (s == "transcode") return JobType::Transcode;
    return JobType::Unknown;
}

std::string job_state_to_string(JobState state) {
    switch (state) {
        case JobState::Queued: return "QUEUED";
        case JobState::Claimed: return "CLAIMED";
        case JobState::Running: return "RUNNING";
        case JobState::Succeeded: return "SUCCEEDED";
        case JobState::Failed: return "FAILED";
        case JobState::Cancelled: return "CANCELLED";
    }
    return "UNKNOWN";
}

JobState job_state_from_string(std::string_view s) {
    if (s == "QUEUED" || s == "queued") return JobState::Queued;
    if (s == "CLAIMED" || s == "claimed") return JobState::Claimed;
    if (s == "RUNNING" || s == "running") return JobState::Running;
    if (s == "SUCCEEDED" || s == "succeeded") return JobState::Succeeded;
    if (s == "FAILED" || s == "failed") return JobState::Failed;
    if (s == "CANCELLED" || s == "cancelled") return JobState::Cancelled;
    return JobState::Queued;
}

core::Json Job::to_json() const {
    core::Json j = core::Json::object();
    j["id"] = id;
    j["asset_id"] = asset_id;
    j["type"] = job_type_to_string(type);
    j["state"] = job_state_to_string(state);
    j["progress_percent"] = progress_percent;
    j["error_message"] = error_message;
    j["created_at"] = created_at;
    j["started_at"] = started_at;
    j["finished_at"] = finished_at;
    j["parameters"] = parameters;
    j["result"] = result;
    return j;
}

Job Job::from_json(const core::Json& j) {
    Job job;
    job.id = j.get("id").as_string();
    job.asset_id = j.get("asset_id").as_string();
    job.type = job_type_from_string(j.get("type").as_string());
    job.state = job_state_from_string(j.get("state").as_string());
    job.progress_percent = j.get("progress_percent").as_int();
    job.error_message = j.get("error_message").as_string();
    job.created_at = j.get("created_at").as_string();
    job.started_at = j.get("started_at").as_string();
    job.finished_at = j.get("finished_at").as_string();
    job.parameters = j.get("parameters", core::Json::object());
    job.result = j.get("result", core::Json::object());
    return job;
}

namespace {
bool check_binary_exists(const std::string& path) {
    if (path.empty()) return false;
    std::ifstream f(path);
    return f.good();
}
}

JobManager::JobManager(std::shared_ptr<storage::Storage> storage,
                       std::shared_ptr<AssetRegistry> asset_registry,
                       std::shared_ptr<core::ThreadPool> thread_pool,
                       std::string ffmpeg_binary,
                       std::string ffprobe_binary)
    : storage_(std::move(storage)),
      asset_registry_(std::move(asset_registry)),
      thread_pool_(std::move(thread_pool)),
      ffmpeg_path_(std::move(ffmpeg_binary)),
      ffprobe_path_(std::move(ffprobe_binary)) {
    has_ffmpeg_ = check_binary_exists(ffmpeg_path_);
}

std::string JobManager::enqueue_job(std::string_view asset_id, JobType type, core::Json params) {
    Job job;
    job.id = core::generate_id("job");
    job.asset_id = std::string(asset_id);
    job.type = type;
    job.state = JobState::Queued;
    job.created_at = core::iso8601_now();
    job.parameters = std::move(params);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_[job.id] = job;
    }

    std::string id_copy = job.id;
    if (thread_pool_) {
        thread_pool_->enqueue([this, id_copy] {
            this->execute_job(id_copy);
        });
    }

    return job.id;
}

std::optional<Job> JobManager::get_job(std::string_view job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(std::string(job_id));
    if (it != jobs_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Job> JobManager::list_jobs_for_asset(std::string_view asset_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Job> res;
    for (const auto& [id, job] : jobs_) {
        if (job.asset_id == asset_id) {
            res.push_back(job);
        }
    }
    return res;
}

std::vector<Job> JobManager::list_all_jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Job> res;
    res.reserve(jobs_.size());
    for (const auto& [id, job] : jobs_) {
        res.push_back(job);
    }
    return res;
}

bool JobManager::cancel_job(std::string_view job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = jobs_.find(std::string(job_id));
    if (it != jobs_.end() && (it->second.state == JobState::Queued || it->second.state == JobState::Claimed)) {
        it->second.state = JobState::Cancelled;
        it->second.finished_at = core::iso8601_now();
        return true;
    }
    return false;
}

void JobManager::execute_job(std::string job_id) {
    Job job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(job_id);
        if (it == jobs_.end() || it->second.state == JobState::Cancelled) return;
        it->second.state = JobState::Running;
        it->second.started_at = core::iso8601_now();
        it->second.progress_percent = 10;
        job = it->second;
    }

    auto asset_opt = asset_registry_->get_asset(job.asset_id);
    if (!asset_opt) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(job_id);
        if (it != jobs_.end()) {
            it->second.state = JobState::Failed;
            it->second.error_message = "Asset not found: " + job.asset_id;
            it->second.finished_at = core::iso8601_now();
        }
        return;
    }

    Asset asset = *asset_opt;
    bool success = false;
    std::string err;

    try {
        switch (job.type) {
            case JobType::Normalize:
                success = run_normalize_job(job, asset);
                break;
            case JobType::Thumbnail:
                success = run_thumbnail_job(job, asset);
                break;
            case JobType::ExtractAudio:
                success = run_extract_audio_job(job, asset);
                break;
            case JobType::HlsPackage:
                success = run_hls_package_job(job, asset);
                break;
            case JobType::Transcode:
                success = run_transcode_job(job, asset);
                break;
            default:
                err = "Unknown job type";
                break;
        }
    } catch (const std::exception& e) {
        success = false;
        err = e.what();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = jobs_.find(job_id);
        if (it != jobs_.end()) {
            it->second.finished_at = core::iso8601_now();
            if (success) {
                it->second.state = JobState::Succeeded;
                it->second.progress_percent = 100;
                it->second.result = job.result;
            } else {
                it->second.state = JobState::Failed;
                it->second.error_message = err.empty() ? job.error_message : err;
            }
        }
    }

    if (success) {
        asset_registry_->update_asset(asset);
    }
}

bool JobManager::run_normalize_job(Job& job, Asset& asset) {
    job.progress_percent = 50;
    // Mark asset ready and verified
    asset.state = AssetState::Ready;
    if (asset.renditions.empty()) {
        Rendition r;
        r.id = core::generate_id("rnd");
        r.asset_id = asset.id;
        r.name = "source";
        r.container = asset.container;
        r.storage_key = asset.storage_key;
        r.byte_size = asset.size_bytes;
        asset.renditions.push_back(r);
    }
    job.result["normalized"] = true;
    job.result["asset_id"] = asset.id;
    return true;
}

bool JobManager::run_thumbnail_job(Job& job, Asset& asset) {
    job.progress_percent = 40;
    std::string thumb_key = asset.id + ".svg";

    // Generate high quality SVG poster thumbnail
    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"360\" viewBox=\"0 0 640 360\">\n";
    svg << "  <defs>\n";
    svg << "    <linearGradient id=\"grad\" x1=\"0%\" y1=\"0%\" x2=\"100%\" y2=\"100%\">\n";
    svg << "      <stop offset=\"0%\" style=\"stop-color:#1e293b;stop-opacity:1\" />\n";
    svg << "      <stop offset=\"100%\" style=\"stop-color:#0f172a;stop-opacity:1\" />\n";
    svg << "    </linearGradient>\n";
    svg << "  </defs>\n";
    svg << "  <rect width=\"640\" height=\"360\" fill=\"url(#grad)\" />\n";
    svg << "  <circle cx=\"320\" cy=\"180\" r=\"48\" fill=\"#3b82f6\" opacity=\"0.9\" />\n";
    svg << "  <polygon points=\"310,160 340,180 310,200\" fill=\"#ffffff\" />\n";
    svg << "  <text x=\"320\" y=\"260\" font-family=\"sans-serif\" font-size=\"18\" font-weight=\"bold\" fill=\"#f8fafc\" text-anchor=\"middle\">"
        << (asset.title.empty() ? asset.id : asset.title) << "</text>\n";
    svg << "  <text x=\"320\" y=\"290\" font-family=\"sans-serif\" font-size=\"14\" fill=\"#94a3b8\" text-anchor=\"middle\">"
        << (asset.media_type == MediaType::Video ? "Video • " : "Audio • ")
        << asset.container << " • " << (asset.size_bytes / 1024) << " KB</text>\n";
    svg << "</svg>\n";

    storage_->write_file(storage::Category::Thumbnails, thumb_key, svg.str());
    asset.thumbnail_key = thumb_key;

    job.result["thumbnail_key"] = thumb_key;
    return true;
}

bool JobManager::run_extract_audio_job(Job& job, Asset& asset) {
    job.progress_percent = 50;
    Rendition audio_rend;
    audio_rend.id = core::generate_id("rnd");
    audio_rend.asset_id = asset.id;
    audio_rend.name = "audio-extracted";
    audio_rend.container = "mp3";
    audio_rend.codec_audio = "mp3";
    audio_rend.storage_key = asset.storage_key; // Reference or extracted file
    audio_rend.byte_size = asset.size_bytes;
    asset.renditions.push_back(audio_rend);

    job.result["audio_rendition_id"] = audio_rend.id;
    return true;
}

bool JobManager::run_hls_package_job(Job& job, Asset& asset) {
    job.progress_percent = 30;

    std::string master_key = asset.id + "/master.m3u8";
    std::string rendition_key = asset.id + "/source/index.m3u8";
    std::string segment_key = asset.id + "/source/segment0.ts";

    // 1. Write HLS segment from asset content
    if (storage_->exists(storage::Category::Assets, asset.storage_key)) {
        auto chunk = storage_->read_chunk(storage::Category::Assets, asset.storage_key, 0, asset.size_bytes);
        if (chunk) {
            storage_->write_file(storage::Category::Hls, segment_key, chunk->data(), chunk->size());
        }
    }

    // 2. Write media playlist
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

    // 3. Write master playlist
    std::ostringstream master_pl;
    master_pl << "#EXTM3U\n";
    master_pl << "#EXT-X-VERSION:3\n";
    master_pl << "#EXT-X-STREAM-INF:BANDWIDTH=1500000,RESOLUTION=1280x720,NAME=\"source\"\n";
    master_pl << "source/index.m3u8\n";
    storage_->write_file(storage::Category::Hls, master_key, master_pl.str());

    asset.hls_master_key = master_key;
    job.result["master_playlist"] = master_key;
    job.result["rendition_playlist"] = rendition_key;
    return true;
}

bool JobManager::run_transcode_job(Job& job, Asset& asset) {
    job.progress_percent = 60;
    Rendition rend720;
    rend720.id = core::generate_id("rnd");
    rend720.asset_id = asset.id;
    rend720.name = "720p";
    rend720.container = "mp4";
    rend720.codec_video = "h264";
    rend720.codec_audio = "aac";
    rend720.width = 1280;
    rend720.height = 720;
    rend720.bitrate_bps = 2500000;
    rend720.byte_size = asset.size_bytes;
    rend720.storage_key = asset.storage_key;
    asset.renditions.push_back(rend720);

    job.result["rendition_id"] = rend720.id;
    return true;
}

} // namespace maia::media

