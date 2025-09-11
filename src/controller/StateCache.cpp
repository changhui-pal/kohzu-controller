#include "controller/StateCache.hpp"

namespace kohzu::controller {

void StateCache::updatePosition(int axis, long pos) {
    std::lock_guard<std::mutex> lk(mtx_);
    positions_[axis] = pos;
}

void StateCache::updateRunning(int axis, bool running) {
    std::lock_guard<std::mutex> lk(mtx_);
    running_[axis] = running;
}

void StateCache::updateRaw(int axis, const std::string& raw) {
    std::lock_guard<std::mutex> lk(mtx_);
    raw_[axis] = raw;
}

bool StateCache::getPosition(int axis, long &out) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = positions_.find(axis);
    if (it == positions_.end()) return false;
    out = it->second;
    return true;
}

bool StateCache::getRunning(int axis, bool &out) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = running_.find(axis);
    if (it == running_.end()) return false;
    out = it->second;
    return true;
}

} // namespace kohzu::controller
