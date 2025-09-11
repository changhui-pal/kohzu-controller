#pragma once
#include <chrono>

namespace kohzu::config {

using ms = std::chrono::milliseconds;

// 기본 응답 대기타임
constexpr std::chrono::milliseconds DEFAULT_RESPONSE_TIMEOUT_MS = std::chrono::milliseconds(60000);

// Poller 기본 간격
constexpr ms DEFAULT_POLL_INTERVAL_MS = ms(500);
constexpr ms DEFAULT_FAST_POLL_INTERVAL_MS = ms(100);

// Writer 기본 최대 큐 크기
constexpr std::size_t DEFAULT_WRITER_MAX_QUEUE = 1000;

// 재연결 간격 (Manager 기본)
constexpr ms DEFAULT_RECONNECT_INTERVAL_MS = ms(5000);

} // namespace kohzu::config
