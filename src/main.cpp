#include "maia/core/service.hpp"
#include <iostream>

int main() {
    maia::core::Service service;
    service.start();
    std::cout << "Maia Streaming v0.1 development skeleton\n";
    std::cout << "Service state: " << (service.running() ? "running" : "stopped") << '\n';
    service.stop();
    return 0;
}
