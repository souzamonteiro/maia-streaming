#include "maia/metrics/metrics.hpp"
#include "maia/core/utils.hpp"
#include <sstream>

namespace maia::metrics {

MetricsRegistry::MetricsRegistry()
    : start_time_epoch_(core::now_seconds()) {}

void MetricsRegistry::record_request(const std::string& method, int status_code) {
    total_requests_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    requests_by_method_status_[{method, status_code}]++;
}

void MetricsRegistry::record_bytes_sent(std::uint64_t bytes) {
    total_bytes_sent_.fetch_add(bytes, std::memory_order_relaxed);
}

void MetricsRegistry::record_range_request() {
    total_range_requests_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::increment_connections() {
    active_connections_.fetch_add(1, std::memory_order_relaxed);
}

void MetricsRegistry::decrement_connections() {
    active_connections_.fetch_sub(1, std::memory_order_relaxed);
}

void MetricsRegistry::set_active_rooms(std::size_t count) {
    active_rooms_.store(count, std::memory_order_relaxed);
}

void MetricsRegistry::set_active_participants(std::size_t count) {
    active_participants_.store(count, std::memory_order_relaxed);
}

void MetricsRegistry::record_job_completed(const std::string& type, bool success) {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_by_type_success_[{type, success}]++;
}

std::uint64_t MetricsRegistry::uptime_seconds() const noexcept {
    auto now = core::now_seconds();
    return (now >= start_time_epoch_) ? (now - start_time_epoch_) : 0;
}

std::string MetricsRegistry::format_prometheus() const {
    std::ostringstream ss;

    ss << "# HELP maia_uptime_seconds Maia streaming server uptime in seconds\n";
    ss << "# TYPE maia_uptime_seconds counter\n";
    ss << "maia_uptime_seconds " << uptime_seconds() << "\n\n";

    ss << "# HELP maia_http_requests_total Total HTTP requests handled\n";
    ss << "# TYPE maia_http_requests_total counter\n";
    ss << "maia_http_requests_total " << total_requests_.load() << "\n";

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [key, count] : requests_by_method_status_) {
            ss << "maia_http_requests_by_status_total{method=\"" << key.first
               << "\",code=\"" << key.second << "\"} " << count << "\n";
        }
    }
    ss << "\n";

    ss << "# HELP maia_http_bytes_sent_total Total media bytes delivered\n";
    ss << "# TYPE maia_http_bytes_sent_total counter\n";
    ss << "maia_http_bytes_sent_total " << total_bytes_sent_.load() << "\n\n";

    ss << "# HELP maia_range_requests_total Total HTTP 206 Partial Content range requests\n";
    ss << "# TYPE maia_range_requests_total counter\n";
    ss << "maia_range_requests_total " << total_range_requests_.load() << "\n\n";

    ss << "# HELP maia_active_connections Current active client connections\n";
    ss << "# TYPE maia_active_connections gauge\n";
    ss << "maia_active_connections " << active_connections_.load() << "\n\n";

    ss << "# HELP maia_webrtc_active_rooms Current active WebRTC rooms\n";
    ss << "# TYPE maia_webrtc_active_rooms gauge\n";
    ss << "maia_webrtc_active_rooms " << active_rooms_.load() << "\n\n";

    ss << "# HELP maia_webrtc_active_participants Current connected WebRTC participants\n";
    ss << "# TYPE maia_webrtc_active_participants gauge\n";
    ss << "maia_webrtc_active_participants " << active_participants_.load() << "\n\n";

    ss << "# HELP maia_jobs_completed_total Media processing jobs executed\n";
    ss << "# TYPE maia_jobs_completed_total counter\n";
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [key, count] : jobs_by_type_success_) {
            ss << "maia_jobs_completed_total{type=\"" << key.first
               << "\",status=\"" << (key.second ? "success" : "failed") << "\"} " << count << "\n";
        }
    }

    return ss.str();
}

core::Json MetricsRegistry::format_healthz() const {
    core::Json resp = core::Json::object();
    resp["status"] = "ok";
    resp["uptime_seconds"] = static_cast<double>(uptime_seconds());
    resp["total_requests"] = static_cast<double>(total_requests_.load());
    resp["active_connections"] = static_cast<double>(active_connections_.load());
    return resp;
}

core::Json MetricsRegistry::format_readyz(bool storage_ok, bool workers_ok) const {
    core::Json resp = core::Json::object();
    bool ready = storage_ok && workers_ok;
    resp["status"] = ready ? "ready" : "not_ready";
    resp["storage_ok"] = storage_ok;
    resp["workers_ok"] = workers_ok;
    resp["uptime_seconds"] = static_cast<double>(uptime_seconds());
    return resp;
}

} // namespace maia::metrics
