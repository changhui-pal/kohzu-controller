#pragma once
/**
 * Config.hpp
 *
 * 전역 기본 구성값(리팩토링 권장안).
 *
 * 필요에 따라 여기 값을 프로그램 시작시 환경변수/설정파일로 읽어오도록 확장 가능.
 */

#include <chrono>

namespace kohzu::config {

using ms = std::chrono::milliseconds;

// 응답 대기 기본 타임아웃 (sendSync의 기본값)
constexpr std::chrono::milliseconds DEFAULT_RESPONSE_TIMEOUT_MS = std::chrono::milliseconds(60000);

// Poller 기본 간격(일반/빠른)
constexpr ms DEFAULT_POLL_INTERVAL_MS = ms(500);
constexpr ms DEFAULT_FAST_POLL_INTERVAL_MS = ms(100);

// Writer 기본 최대 큐 크기
constexpr std::size_t DEFAULT_WRITER_MAX_QUEUE = 1000;

// 재연결 간격 (Manager 기본)
constexpr ms DEFAULT_RECONNECT_INTERVAL_MS = ms(5000);

} // namespace kohzu::config
