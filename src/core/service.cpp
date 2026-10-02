#include "maia/core/service.hpp"
#include "maia/core/utils.hpp"
#include "maia/vod/range.hpp"
#include <iostream>

namespace maia::core {

Service::Service(Config config)
    : config_(std::move(config)) {
    // 1. Storage
    storage_ = std::make_shared<storage::FilesystemStorage>(config_.storage.root);

    // 2. Thread Pool for async jobs
    thread_pool_ = std::make_shared<core::ThreadPool>(
        config_.processing.max_parallel_jobs, 1024);

    // 3. Asset Registry
    asset_registry_ = std::make_shared<media::AssetRegistry>(storage_);
    asset_registry_->init();

    // 4. Job Manager
    job_manager_ = std::make_shared<media::JobManager>(
        storage_, asset_registry_, thread_pool_,
        config_.processing.ffmpeg, config_.processing.ffprobe);

    // 5. Recording Manager
    recording_manager_ = std::make_shared<media::RecordingManager>(
        storage_, asset_registry_, job_manager_);

    // 6. Token Manager
    token_manager_ = std::make_shared<auth::TokenManager>(
        config_.auth.secret_key, config_.auth.issuer);

    // 7. HLS Manager
    hls_manager_ = std::make_shared<hls::HlsManager>(
        storage_, asset_registry_);

    // 8. Metrics
    metrics_ = std::make_shared<metrics::MetricsRegistry>();

    // 9. WebRTC SFU
    sfu_server_ = std::make_shared<webrtc::SfuServer>();

    // 10. HTTP Server
    http_server_ = std::make_shared<http::HttpServer>(
        config_.server.bind, config_.server.port,
        config_.server.worker_threads, config_.server.max_connections);

    setup_routes();
}

Service::~Service() {
    stop();
}

bool Service::start() noexcept {
    if (running_.load()) return true;

    try {
        if (!http_server_->start()) {
            std::cerr << "[Maia-Streaming] Failed to bind HTTP server to "
                      << config_.server.bind << ":" << config_.server.port << std::endl;
            return false;
        }
        running_.store(true);
        std::cout << "[Maia-Streaming] HTTP media server listening on http://"
                  << config_.server.bind << ":" << config_.server.port << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Maia-Streaming] Error starting service: " << e.what() << std::endl;
        return false;
    }
}

void Service::stop() noexcept {
    if (!running_.exchange(false)) return;

    if (http_server_) {
        http_server_->stop();
    }
    if (thread_pool_) {
        thread_pool_->stop();
    }
    std::cout << "[Maia-Streaming] Service stopped successfully." << std::endl;
}

bool Service::running() const noexcept {
    return running_.load();
}

bool Service::verify_playback_auth(const http::HttpRequest& req, std::string_view asset_id, std::string_view op) const {
    if (!config_.auth.require_tokens) {
        return true; // Token optional in dev/open mode
    }

    std::string token = req.get_param("token");
    if (token.empty()) {
        std::string auth_hdr = req.get_header("Authorization");
        if (auth_hdr.starts_with("Bearer ") || auth_hdr.starts_with("bearer ")) {
            token = core::trim(auth_hdr.substr(7));
        }
    }

    if (token.empty()) {
        return false;
    }

    auto claims = token_manager_->verify_token(token, asset_id, op);
    return claims.has_value();
}

void Service::setup_routes() {
    // ------------------------------------------------------------------------
    // Health and Metrics
    // ------------------------------------------------------------------------
    http_server_->get("/healthz", [this](const http::HttpRequest& /*req*/) {
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, metrics_->format_healthz());
    });

    http_server_->get("/readyz", [this](const http::HttpRequest& /*req*/) {
        bool storage_ok = storage_->is_accessible();
        bool workers_ok = (thread_pool_ != nullptr);
        int code = (storage_ok && workers_ok) ? 200 : 503;
        metrics_->record_request("GET", code);
        return http::HttpResponse::json(code, metrics_->format_readyz(storage_ok, workers_ok));
    });

    http_server_->get("/metrics", [this](const http::HttpRequest& /*req*/) {
        metrics_->set_active_rooms(sfu_server_->active_rooms_count());
        metrics_->set_active_participants(sfu_server_->active_participants_count());
        metrics_->record_request("GET", 200);
        return http::HttpResponse::text(200, metrics_->format_prometheus(), "text/plain; version=0.0.4");
    });

    // ------------------------------------------------------------------------
    // Playback Capability Token
    // ------------------------------------------------------------------------
    http_server_->post("/v1/assets/{assetId}/playback-token", [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");
        auto asset = asset_registry_->get_asset(asset_id);
        if (!asset) {
            metrics_->record_request("POST", 404);
            return http::HttpResponse::error(404, "not_found", "Asset not found: " + asset_id);
        }

        std::uint64_t ttl = 3600; // default 1 hour
        std::string op = "play";
        std::string sub = "user";

        if (!req.body.empty()) {
            auto j = core::Json::parse(req.body);
            if (j && j->is_object()) {
                if (j->contains("ttl_seconds")) ttl = j->get("ttl_seconds").as_uint64(3600);
                if (j->contains("op")) op = j->get("op").as_string("play");
                if (j->contains("sub")) sub = j->get("sub").as_string("user");
            }
        }

        std::string token = token_manager_->create_token(asset_id, ttl, op, sub);
        core::Json resp = core::Json::object();
        resp["token"] = token;
        resp["asset_id"] = asset_id;
        resp["expires_at"] = core::now_seconds() + ttl;
        resp["op"] = op;
        resp["sub"] = sub;

        metrics_->record_request("POST", 200);
        return http::HttpResponse::json(200, resp);
    });

    // ------------------------------------------------------------------------
    // VOD Content Delivery (GET & HEAD with Range support)
    // ------------------------------------------------------------------------
    auto handle_vod_content = [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");

        // 1. Authorization check
        if (!verify_playback_auth(req, asset_id, "play")) {
            metrics_->record_request(req.method, 401);
            return http::HttpResponse::error(401, "unauthorized", "Valid playback token required for asset " + asset_id);
        }

        // 2. Asset lookup
        auto asset = asset_registry_->get_asset(asset_id);
        if (!asset) {
            metrics_->record_request(req.method, 404);
            return http::HttpResponse::error(404, "not_found", "Asset not found: " + asset_id);
        }

        // 3. Storage lookup
        auto file_stat = storage_->stat(storage::Category::Assets, asset->storage_key);
        if (!file_stat) {
            metrics_->record_request(req.method, 404);
            return http::HttpResponse::error(404, "media_missing", "Underlying media file missing for asset: " + asset_id);
        }

        auto file_path = storage_->get_path(storage::Category::Assets, asset->storage_key);
        if (!file_path) {
            metrics_->record_request(req.method, 500);
            return http::HttpResponse::error(500, "storage_error", "Cannot resolve media path");
        }

        uint64_t total_size = file_stat->size;
        std::string etag = file_stat->etag;

        // 4. Conditional GET (If-None-Match)
        std::string if_none_match = req.get_header("If-None-Match");
        if (!if_none_match.empty() && (if_none_match == etag || if_none_match == "*")) {
            http::HttpResponse not_modified;
            not_modified.status_code = 304;
            not_modified.status_message = "Not Modified";
            not_modified.set_header("ETag", etag);
            not_modified.set_header("Accept-Ranges", "bytes");
            metrics_->record_request(req.method, 304);
            return not_modified;
        }

        // 5. Byte Range Evaluation
        std::string range_header = req.get_header("Range");
        if (range_header.empty()) {
            // Full content (200 OK)
            metrics_->record_request(req.method, 200);
            metrics_->record_bytes_sent(total_size);
            return http::HttpResponse::file(200, *file_path, 0, total_size, asset->mime_type, "", etag);
        }

        // If-Range check
        std::string if_range = req.get_header("If-Range");
        if (!if_range.empty() && if_range != etag) {
            // ETag mismatch -> deliver 200 OK full content instead of 206
            metrics_->record_request(req.method, 200);
            metrics_->record_bytes_sent(total_size);
            return http::HttpResponse::file(200, *file_path, 0, total_size, asset->mime_type, "", etag);
        }

        auto byte_range = vod::parseByteRange(range_header, total_size);
        if (!byte_range) {
            // 416 Range Not Satisfiable
            metrics_->record_request(req.method, 416);
            http::HttpResponse resp = http::HttpResponse::error(416, "range_not_satisfiable", "Requested range is not satisfiable");
            resp.set_header("Content-Range", vod::formatContentRangeUnsatisfiable(total_size));
            resp.set_header("Accept-Ranges", "bytes");
            return resp;
        }

        // 206 Partial Content
        metrics_->record_request(req.method, 206);
        metrics_->record_range_request();
        metrics_->record_bytes_sent(byte_range->length());

        std::string content_range = vod::formatContentRange(*byte_range, total_size);
        return http::HttpResponse::file(206, *file_path, byte_range->first, byte_range->length(), asset->mime_type, content_range, etag);
    };

    http_server_->get("/v1/media/{assetId}/content", handle_vod_content);

    // ------------------------------------------------------------------------
    // HLS Adaptive VOD Delivery
    // ------------------------------------------------------------------------
    http_server_->get("/v1/hls/{assetId}/master.m3u8", [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");
        if (!verify_playback_auth(req, asset_id, "play")) {
            metrics_->record_request("GET", 401);
            return http::HttpResponse::error(401, "unauthorized", "Playback token required");
        }

        auto pl = hls_manager_->get_master_playlist(asset_id);
        if (!pl) {
            metrics_->record_request("GET", 404);
            return http::HttpResponse::error(404, "not_found", "HLS master playlist not found for asset: " + asset_id);
        }

        metrics_->record_request("GET", 200);
        return http::HttpResponse::text(200, *pl, "application/vnd.apple.mpegurl");
    });

    http_server_->get("/v1/hls/{assetId}/{rendition}/index.m3u8", [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");
        std::string rendition = req.get_param("rendition");
        if (!verify_playback_auth(req, asset_id, "play")) {
            metrics_->record_request("GET", 401);
            return http::HttpResponse::error(401, "unauthorized", "Playback token required");
        }

        auto pl = hls_manager_->get_media_playlist(asset_id, rendition);
        if (!pl) {
            metrics_->record_request("GET", 404);
            return http::HttpResponse::error(404, "not_found", "HLS media playlist not found");
        }

        metrics_->record_request("GET", 200);
        return http::HttpResponse::text(200, *pl, "application/vnd.apple.mpegurl");
    });

    http_server_->get("/v1/hls/{assetId}/{rendition}/{segment}", [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");
        std::string rendition = req.get_param("rendition");
        std::string segment = req.get_param("segment");
        if (!verify_playback_auth(req, asset_id, "play")) {
            metrics_->record_request("GET", 401);
            return http::HttpResponse::error(401, "unauthorized", "Playback token required");
        }

        std::string key = asset_id + "/" + rendition + "/" + segment;
        auto path_opt = storage_->get_path(storage::Category::Hls, key);
        if (path_opt && storage_->exists(storage::Category::Hls, key)) {
            auto st = storage_->stat(storage::Category::Hls, key);
            if (st) {
                metrics_->record_request("GET", 200);
                std::string mime = segment.ends_with(".m4s") ? "video/iso.segment" : "video/MP2T";
                return http::HttpResponse::file(200, *path_opt, 0, st->size, mime);
            }
        }

        // Fallback: get segment bytes directly
        auto chunk = hls_manager_->get_segment(asset_id, rendition, segment);
        if (!chunk) {
            metrics_->record_request("GET", 404);
            return http::HttpResponse::error(404, "not_found", "Segment not found: " + segment);
        }

        metrics_->record_request("GET", 200);
        http::HttpResponse resp;
        resp.status_code = 200;
        resp.status_message = "OK";
        resp.set_header("Content-Type", segment.ends_with(".m4s") ? "video/iso.segment" : "video/MP2T");
        resp.set_header("Cache-Control", "public, max-age=86400, immutable");
        resp.body.assign(chunk->begin(), chunk->end());
        resp.set_header("Content-Length", std::to_string(resp.body.size()));
        return resp;
    });

    // ------------------------------------------------------------------------
    // Assets CRUD
    // ------------------------------------------------------------------------
    http_server_->get("/v1/assets", [this](const http::HttpRequest& req) {
        std::string owner = req.get_param("owner");
        auto assets = asset_registry_->list_assets(owner);
        core::Json arr = core::Json::array();
        for (const auto& a : assets) {
            arr.push_back(a.to_json());
        }
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, arr);
    });

    http_server_->get("/v1/assets/{assetId}", [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");
        auto asset = asset_registry_->get_asset(asset_id);
        if (!asset) {
            metrics_->record_request("GET", 404);
            return http::HttpResponse::error(404, "not_found", "Asset not found: " + asset_id);
        }
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, asset->to_json());
    });

    http_server_->del("/v1/assets/{assetId}", [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");
        if (!asset_registry_->delete_asset(asset_id)) {
            metrics_->record_request("DELETE", 404);
            return http::HttpResponse::error(404, "not_found", "Asset not found: " + asset_id);
        }
        metrics_->record_request("DELETE", 200);
        core::Json resp = core::Json::object();
        resp["deleted"] = true;
        resp["asset_id"] = asset_id;
        return http::HttpResponse::json(200, resp);
    });

    http_server_->post("/v1/assets", [this](const http::HttpRequest& req) {
        std::string filename = req.get_header("X-Filename", req.get_param("filename", "upload.mp4"));
        std::string binary_data = req.body;
        std::string title_override;
        std::string owner_override;

        if (req.get_header("Content-Type").find("json") != std::string::npos && !req.body.empty()) {
            auto j = core::Json::parse(req.body);
            if (j && j->is_object()) {
                if (j->contains("name")) filename = j->get("name").as_string();
                if (j->contains("filename")) filename = j->get("filename").as_string();
                if (j->contains("title")) title_override = j->get("title").as_string();
                if (j->contains("owner")) owner_override = j->get("owner").as_string();
                if (j->contains("data_base64")) {
                    auto decoded = core::base64_decode(j->get("data_base64").as_string());
                    if (decoded) {
                        binary_data.assign(reinterpret_cast<const char*>(decoded->data()), decoded->size());
                    }
                }
            }
        }
        filename = core::sanitize_filename(filename);

        media::Asset asset;
        asset.id = core::generate_id("01J");
        asset.title = !title_override.empty() ? title_override : req.get_param("title", filename);
        asset.owner = !owner_override.empty() ? owner_override : req.get_param("owner", "default");
        asset.created_at = core::iso8601_now();

        std::string dot_ext;
        auto dot = filename.find_last_of('.');
        if (dot != std::string_view::npos) {
            dot_ext = filename.substr(dot);
        } else {
            dot_ext = ".mp4";
        }
        asset.storage_key = asset.id + dot_ext;

        if (!binary_data.empty()) {
            // Write binary payload to storage
            storage_->write_file(storage::Category::Assets, asset.storage_key,
                                 reinterpret_cast<const uint8_t*>(binary_data.data()), binary_data.size());
            asset.size_bytes = binary_data.size();

            // Probe media
            auto probed = media::probe_media(
                reinterpret_cast<const uint8_t*>(binary_data.data()),
                std::min<std::size_t>(binary_data.size(), 64), filename);

            asset.media_type = probed.media_type;
            asset.container = probed.container;
            asset.mime_type = probed.mime_type;
            asset.duration_ms = probed.duration_ms;
        } else {
            asset.size_bytes = 0;
            asset.mime_type = core::mime_type_from_filename(filename);
            asset.container = dot_ext.empty() ? "mp4" : dot_ext.substr(1);
        }

        asset_registry_->add_asset(asset);

        // Enqueue auto thumbnail & HLS packaging jobs
        (void)job_manager_->enqueue_job(asset.id, media::JobType::Thumbnail);
        (void)job_manager_->enqueue_job(asset.id, media::JobType::HlsPackage);

        metrics_->record_request("POST", 201);
        return http::HttpResponse::json(201, asset.to_json());
    });

    // ------------------------------------------------------------------------
    // Processing Jobs API
    // ------------------------------------------------------------------------
    http_server_->post("/v1/assets/{assetId}/jobs", [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");
        auto asset = asset_registry_->get_asset(asset_id);
        if (!asset) {
            metrics_->record_request("POST", 404);
            return http::HttpResponse::error(404, "not_found", "Asset not found: " + asset_id);
        }

        std::string type_str = "normalize";
        core::Json params = core::Json::object();
        if (!req.body.empty()) {
            auto j = core::Json::parse(req.body);
            if (j && j->is_object()) {
                type_str = j->get("type").as_string("normalize");
                params = j->get("parameters", core::Json::object());
            }
        }

        auto job_type = media::job_type_from_string(type_str);
        if (job_type == media::JobType::Unknown) {
            metrics_->record_request("POST", 400);
            return http::HttpResponse::error(400, "invalid_job_type", "Supported job types: normalize, thumbnail, extract-audio, hls-package, transcode");
        }

        std::string job_id = job_manager_->enqueue_job(asset_id, job_type, params);
        auto job = job_manager_->get_job(job_id);

        metrics_->record_request("POST", 201);
        return http::HttpResponse::json(201, job ? job->to_json() : core::Json::object());
    });

    http_server_->get("/v1/jobs/{jobId}", [this](const http::HttpRequest& req) {
        std::string job_id = req.get_param("jobId");
        auto job = job_manager_->get_job(job_id);
        if (!job) {
            metrics_->record_request("GET", 404);
            return http::HttpResponse::error(404, "not_found", "Job not found: " + job_id);
        }
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, job->to_json());
    });

    http_server_->get("/v1/assets/{assetId}/jobs", [this](const http::HttpRequest& req) {
        std::string asset_id = req.get_param("assetId");
        auto jobs = job_manager_->list_jobs_for_asset(asset_id);
        core::Json arr = core::Json::array();
        for (const auto& j : jobs) {
            arr.push_back(j.to_json());
        }
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, arr);
    });

    // ------------------------------------------------------------------------
    // Recording API
    // ------------------------------------------------------------------------
    http_server_->post("/v1/recordings", [this](const http::HttpRequest& req) {
        std::string room_id = "default";
        std::string title = "";
        if (!req.body.empty()) {
            auto j = core::Json::parse(req.body);
            if (j && j->is_object()) {
                room_id = j->get("room_id").as_string("default");
                title = j->get("title").as_string("");
            }
        }

        auto rec = recording_manager_->start_recording(room_id, title);
        if (!rec) {
            metrics_->record_request("POST", 500);
            return http::HttpResponse::error(500, "recording_failed", "Failed to start recording session");
        }

        // Attach to room if active
        auto room = sfu_server_->get_room(room_id);
        if (room) {
            room->attach_recording(rec);
        }

        metrics_->record_request("POST", 201);
        return http::HttpResponse::json(201, rec->to_json());
    });

    http_server_->post("/v1/recordings/{recordingId}/stop", [this](const http::HttpRequest& req) {
        std::string rec_id = req.get_param("recordingId");
        auto rec = recording_manager_->get_recording(rec_id);
        if (!rec) {
            metrics_->record_request("POST", 404);
            return http::HttpResponse::error(404, "not_found", "Recording not found: " + rec_id);
        }

        auto room = sfu_server_->get_room(rec->room_id());
        if (room) {
            room->detach_recording();
        }

        if (!recording_manager_->stop_recording(rec_id)) {
            metrics_->record_request("POST", 500);
            return http::HttpResponse::error(500, "stop_failed", "Failed to finalize recording");
        }

        metrics_->record_request("POST", 200);
        return http::HttpResponse::json(200, rec->to_json());
    });

    http_server_->get("/v1/recordings/{recordingId}", [this](const http::HttpRequest& req) {
        std::string rec_id = req.get_param("recordingId");
        auto rec = recording_manager_->get_recording(rec_id);
        if (!rec) {
            metrics_->record_request("GET", 404);
            return http::HttpResponse::error(404, "not_found", "Recording not found: " + rec_id);
        }
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, rec->to_json());
    });

    http_server_->get("/v1/recordings", [this](const http::HttpRequest& /*req*/) {
        auto recs = recording_manager_->list_recordings();
        core::Json arr = core::Json::array();
        for (const auto& r : recs) {
            arr.push_back(r->to_json());
        }
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, arr);
    });

    // ------------------------------------------------------------------------
    // WebRTC SFU API
    // ------------------------------------------------------------------------
    http_server_->get("/v1/webrtc/rooms", [this](const http::HttpRequest& /*req*/) {
        auto rooms = sfu_server_->list_rooms();
        core::Json arr = core::Json::array();
        for (const auto& r : rooms) {
            arr.push_back(r->to_json());
        }
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, arr);
    });

    http_server_->post("/v1/webrtc/rooms", [this](const http::HttpRequest& req) {
        std::string room_id = "";
        std::string name = "";
        if (!req.body.empty()) {
            auto j = core::Json::parse(req.body);
            if (j && j->is_object()) {
                room_id = j->get("room_id").as_string();
                name = j->get("name").as_string();
            }
        }
        auto room = sfu_server_->create_room(room_id, name);
        metrics_->record_request("POST", 201);
        return http::HttpResponse::json(201, room->to_json());
    });

    http_server_->get("/v1/webrtc/rooms/{roomId}", [this](const http::HttpRequest& req) {
        std::string room_id = req.get_param("roomId");
        auto room = sfu_server_->get_room(room_id);
        if (!room) {
            metrics_->record_request("GET", 404);
            return http::HttpResponse::error(404, "not_found", "Room not found: " + room_id);
        }
        metrics_->record_request("GET", 200);
        return http::HttpResponse::json(200, room->to_json());
    });

    http_server_->post("/v1/webrtc/rooms/{roomId}/signal", [this](const http::HttpRequest& req) {
        std::string room_id = req.get_param("roomId");
        auto j = core::Json::parse(req.body);
        if (!j || !j->is_object()) {
            metrics_->record_request("POST", 400);
            return http::HttpResponse::error(400, "invalid_json", "Invalid JSON signal message");
        }

        auto resp = sfu_server_->handle_signal(room_id, *j);
        metrics_->record_request("POST", 200);
        return http::HttpResponse::json(200, resp);
    });

    // ------------------------------------------------------------------------
    // Web Player and UI Delivery
    // ------------------------------------------------------------------------
    auto handle_player = [](const http::HttpRequest& /*req*/) {
        std::filesystem::path player_file = "examples/player/index.html";
        std::error_code ec;
        if (!std::filesystem::exists(player_file, ec)) {
            // Check absolute path
            player_file = "/home/roberto/projects/maia-streaming/examples/player/index.html";
        }

        if (std::filesystem::exists(player_file, ec)) {
            auto sz = std::filesystem::file_size(player_file, ec);
            return http::HttpResponse::file(200, player_file, 0, sz, "text/html; charset=utf-8");
        }

        return http::HttpResponse::text(200, "<h1>Maia Streaming Server</h1><p>Player UI at examples/player/index.html</p>", "text/html; charset=utf-8");
    };

    http_server_->get("/", handle_player);
    http_server_->get("/player", handle_player);
    http_server_->get("/player/", handle_player);
    http_server_->get("/player/index.html", handle_player);
}

} // namespace maia::core
