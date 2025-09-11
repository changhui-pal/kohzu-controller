#pragma once
#include <mutex>
#include <unordered_map>
#include <string>

namespace kohzu::controller {

class StateCache {
public:
    void updatePosition(int axis, long pos);
    void updateRunning(int axis, bool running);
    void updateRaw(int axis, const std::string& raw);

    // optional getters
    bool getPosition(int axis, long &out) const;
    bool getRunning(int axis, bool &out) const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<int, long> positions_;
    std::unordered_map<int, bool> running_;
    std::unordered_map<int, std::string> raw_;
};

} // namespace kohzu::controller
