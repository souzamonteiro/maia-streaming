#include "maia/core/service.hpp"
namespace maia::core {
void Service::start() noexcept { running_.store(true); }
void Service::stop() noexcept { running_.store(false); }
bool Service::running() const noexcept { return running_.load(); }
}
