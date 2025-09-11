#pragma once
/**
 * StateCache.hpp (리팩토링안)
 *
 * 축(axis)별 상태를 스레드-안전하게 저장하고 조회하는 단순한 캐시.
 *
 * 설계 목표:
 *  - Poller가 빈번하게 위치(position)를 갱신하도록 효율적이고 안전하게 동작
 *  - Manager/GUI가 읽을 때 원자적 스냅샷을 얻을 수 있도록 snapshot() 제공
 *  - 간단한 updatePosition/updateRunning/updateRaw API 제공
 *
 * 사용법 예:
 *   StateCache cache;
 *   cache.updatePosition(1, 12345);
 *   cache.updateRunning(1, true);
 *   auto snap = cache.snapshot();
 *   auto st = cache.get(1);
 */

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <mutex>

namespace kohzu::controller {

struct AxisState {
    // position: 장비에서 보고하는 절대 위치 (응답 파싱 후 저장)
    // - NaN 같은 특별값을 쓰지 않기 위해 optional 사용
    std::optional<int64_t> position;

    // running: 이동 중 상태(STR 등에서 얻음)
    std::optional<bool> running;

    // raw: 마지막으로 수신된 원시 라인(디버깅/로그용)
    std::string raw;

    // timestamp: 마지막 갱신 시각 (steady_clock)
    std::chrono::steady_clock::time_point lastUpdated;

    AxisState()
        : position(std::nullopt),
          running(std::nullopt),
          raw(),
          lastUpdated(std::chrono::steady_clock::now()) {}
};

class StateCache {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    StateCache() = default;
    ~StateCache() = default;

    // non-copyable (but movable if needed)
    StateCache(const StateCache&) = delete;
    StateCache& operator=(const StateCache&) = delete;

    // updatePosition: axis의 위치를 저장하고 timestamp 갱신
    void updatePosition(int axis, int64_t position) {
        std::lock_guard<std::mutex> lk(mtx_);
        AxisState &st = data_[axis];
        st.position = position;
        st.lastUpdated = Clock::now();
    }

    // updateRunning: axis의 running 상태를 저장
    void updateRunning(int axis, bool running) {
        std::lock_guard<std::mutex> lk(mtx_);
        AxisState &st = data_[axis];
        st.running = running;
        st.lastUpdated = Clock::now();
    }

    // updateRaw: 원시 응답 문자열을 저장(디버깅/추적용)
    void updateRaw(int axis, const std::string& raw) {
        std::lock_guard<std::mutex> lk(mtx_);
        AxisState &st = data_[axis];
        st.raw = raw;
        st.lastUpdated = Clock::now();
    }

    // update: 복합 업데이트 (position, running, raw 중 가능한 항목만 갱신)
    // 파라미터은 optional로 전달; 예: update(axis, std::optional<int64_t>{pos}, std::nullopt, rawStr)
    void update(int axis,
                const std::optional<int64_t>& position,
                const std::optional<bool>& running,
                const std::optional<std::string>& raw) {
        std::lock_guard<std::mutex> lk(mtx_);
        AxisState &st = data_[axis];
        if (position.has_value()) st.position = position.value();
        if (running.has_value()) st.running = running.value();
        if (raw.has_value()) st.raw = *raw;
        st.lastUpdated = Clock::now();
    }

    // get: 특정 axis의 현재 상태를 복사하여 반환(없으면 false)
    // 복사본 반환이므로 호출자는 자유롭게 읽을 수 있음
    bool get(int axis, AxisState &out) const {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = data_.find(axis);
        if (it == data_.end()) return false;
        out = it->second;
        return true;
    }

    // snapshot: 모든 axis 상태를 원자적으로 복사하여 반환
    std::unordered_map<int, AxisState> snapshot() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return data_; // copy
    }

    // clear: 캐시 초기화
    void clear() {
        std::lock_guard<std::mutex> lk(mtx_);
        data_.clear();
    }

    // exists: axis가 존재하는지 여부 (읽기 전용 helper)
    bool exists(int axis) const {
        std::lock_guard<std::mutex> lk(mtx_);
        return data_.find(axis) != data_.end();
    }

private:
    mutable std::mutex mtx_;
    std::unordered_map<int, AxisState> data_;
};

} // namespace kohzu::controller