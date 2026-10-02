#pragma once

#include <string>
#include <map>
#include <atomic>
#include <mutex>
#include <cstdint>
#include "maia/core/json.hpp"

namespace maia::metrics {

class MetricsRegistry {
public:
    MetricsRegistry();

    void record_request(const std::string& method, int status_code);
    void record_bytes_sent(std::uint64_t bytes);
    void record_range_request();
    void increment_connections();
    void decrement_connections();
    void set_active_rooms(std::size_t count);
    void set_active_participants(std::size_t count);
    void record_job_completed(const std::string& type, bool success);

    [[nodiscard]] std::string format_prometheus() const;
    [[nodiscard]] core::Json format_healthz() const;
    [[nodiscard]] core::Json format_readyz(bool storage_ok, bool workers_ok) const;
    [[nodiscard]] std::uint64_t uptime_seconds() const noexcept;

private:
    std::uint64_t start_time_epoch_{0};
    std::atomic<std::uint64_t> total_requests_{0};
    std::atomic<std::uint64_t> total_bytes_sent_{0};
    std::atomic<std::uint64_t> total_range_requests_{0};
    std::atomic<std::int64_t> active_connections_{0};
    std::atomic<std::size_t> active_rooms_{0};
    std::atomic<std::size_t> active_participants_{0};

    mutable std::mutex mutex_;
    std::map<std::pair<std::string, int>, std::uint64_t> requests_by_method_status_;
    std::map<std::pair<std::string, bool>, std::uint64_t> jobs_by_type_success_;
};

} // namespace maia::metrics
