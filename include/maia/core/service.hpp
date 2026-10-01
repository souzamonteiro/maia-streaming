#pragma once

#include <memory>
#include <atomic>
#include <string>
#include "maia/core/config.hpp"
#include "maia/core/thread_pool.hpp"
#include "maia/storage/storage.hpp"
#include "maia/auth/token.hpp"
#include "maia/media/asset.hpp"
#include "maia/media/job.hpp"
#include "maia/media/recording.hpp"
#include "maia/hls/hls.hpp"
#include "maia/metrics/metrics.hpp"
#include "maia/webrtc/sfu.hpp"
#include "maia/http/http_server.hpp"

namespace maia::core {

class Service {
public:
    explicit Service(Config config = Config::default_config());
    ~Service();

    bool start() noexcept;
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;

    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] std::shared_ptr<storage::Storage> storage() const noexcept { return storage_; }
    [[nodiscard]] std::shared_ptr<media::AssetRegistry> asset_registry() const noexcept { return asset_registry_; }
    [[nodiscard]] std::shared_ptr<media::JobManager> job_manager() const noexcept { return job_manager_; }
    [[nodiscard]] std::shared_ptr<media::RecordingManager> recording_manager() const noexcept { return recording_manager_; }
    [[nodiscard]] std::shared_ptr<auth::TokenManager> token_manager() const noexcept { return token_manager_; }
    [[nodiscard]] std::shared_ptr<hls::HlsManager> hls_manager() const noexcept { return hls_manager_; }
    [[nodiscard]] std::shared_ptr<metrics::MetricsRegistry> metrics() const noexcept { return metrics_; }
    [[nodiscard]] std::shared_ptr<webrtc::SfuServer> sfu_server() const noexcept { return sfu_server_; }
    [[nodiscard]] std::shared_ptr<http::HttpServer> http_server() const noexcept { return http_server_; }

private:
    Config config_;
    std::atomic_bool running_{false};

    std::shared_ptr<storage::Storage> storage_;
    std::shared_ptr<core::ThreadPool> thread_pool_;
    std::shared_ptr<media::AssetRegistry> asset_registry_;
    std::shared_ptr<media::JobManager> job_manager_;
    std::shared_ptr<media::RecordingManager> recording_manager_;
    std::shared_ptr<auth::TokenManager> token_manager_;
    std::shared_ptr<hls::HlsManager> hls_manager_;
    std::shared_ptr<metrics::MetricsRegistry> metrics_;
    std::shared_ptr<webrtc::SfuServer> sfu_server_;
    std::shared_ptr<http::HttpServer> http_server_;

    void setup_routes();
    [[nodiscard]] bool verify_playback_auth(const http::HttpRequest& req, std::string_view asset_id, std::string_view op) const;
};

} // namespace maia::core
