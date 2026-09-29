#pragma once
#include <atomic>

namespace maia::core {
class Service {
public:
    void start() noexcept;
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
private:
    std::atomic_bool running_{false};
};
}
