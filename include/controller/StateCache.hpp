// include/controller/StateCache.hpp
#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace kohzu::controller {

struct AxisState {
    int axis = 0;                                   // axis number (1..32)
    std::int64_t position = 0;                      // current motor pulse value
    bool running = false;                           // true if driving (based on STR b field)
    std::chrono::steady_clock::time_point updated;  // last update time
    std::string lastRaw;                            // last raw response (optional)
};

class StateCache {
public:
    StateCache() = default;
    ~StateCache() = default;

    // Update position for axis (thread-safe)
    void updatePosition(int axis, std::int64_t position, const std::string& raw = {}) {
        std::lock_guard<std::mutex> lk(mutex_);
        AxisState& s = map_[axis];
        s.axis = axis;
        s.position = position;
        s.updated = std::chrono::steady_clock::now();
        if (!raw.empty()) s.lastRaw = raw;
    }

    // Update running state for axis (thread-safe)
    void updateRunning(int axis, bool running, const std::string& raw = {}) {
        std::lock_guard<std::mutex> lk(mutex_);
        AxisState& s = map_[axis];
        s.axis = axis;
        s.running = running;
        s.updated = std::chrono::steady_clock::now();
        if (!raw.empty()) s.lastRaw = raw;
    }

    // Combined update convenience
    void update(int axis, std::int64_t position, bool running, const std::string& raw = {}) {
        std::lock_guard<std::mutex> lk(mutex_);
        AxisState& s = map_[axis];
        s.axis = axis;
        s.position = position;
        s.running = running;
        s.updated = std::chrono::steady_clock::now();
        if (!raw.empty()) s.lastRaw = raw;
    }

    // Get copy of AxisState if present
    std::optional<AxisState> get(int axis) const {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = map_.find(axis);
        if (it == map_.end()) return std::nullopt;
        return it->second;
    }

    // Get snapshot of all axis states
    std::unordered_map<int, AxisState> snapshot() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return map_;
    }

    // Clear cache
    void clear() {
        std::lock_guard<std::mutex> lk(mutex_);
        map_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<int, AxisState> map_;
};

} // namespace kohzu::controller

