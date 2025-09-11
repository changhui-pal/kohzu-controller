#pragma once
#include <chrono>

namespace kohzu::config {
using ms = std::chrono::milliseconds;

constexpr std::chrono::milliseconds DEFAULT_RESPONSE_TIMEOUT_MS = std::chrono::milliseconds(60000);
constexpr ms DEFAULT_POLL_INTERVAL_MS = ms(500);
constexpr ms DEFAULT_FAST_POLL_INTERVAL_MS = ms(100);
constexpr std::size_t DEFAULT_WRITER_MAX_QUEUE = 1000;
constexpr ms DEFAULT_RECONNECT_INTERVAL_MS = ms(5000);

} // namespace kohzu::config

