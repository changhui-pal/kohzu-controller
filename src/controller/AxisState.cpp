#include "controller/AxisState.h"
#include <stdexcept>
#include "spdlog/spdlog.h"

/**
 * @brief Updates the current position of a specific axis in a thread-safe manner.
 * @param axisNo The axis number.
 * @param position The new position value.
 */
void AxisState::updatePosition(int axisNo, int position) {
    std::lock_guard<std::mutex> lock(mutex_);
    positions_[axisNo] = position;
    spdlog::debug("Position for axis {} updated to {}", axisNo, position);
}

/**
 * @brief Updates the detailed status of a specific axis from a protocol response.
 * @param axisNo The axis number.
 * @param params A vector of strings containing status parameters.
 */
void AxisState::updateStatus(int axisNo, const std::vector<std::string>& params) {
    if (params.size() < 6) {
        spdlog::warn("Received insufficient status parameters for axis {}. Expected at least 6, got {}.", axisNo, params.size());
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    AxisStatus newStatus;
    try {
        newStatus.drivingState = std::stoi(params[0]);
        newStatus.emgSignal = std::stoi(params[1]);
        newStatus.orgNorgSignal = std::stoi(params[2]);
        newStatus.cwCcwLimitSignal = std::stoi(params[3]);
        newStatus.softLimitState = std::stoi(params[4]);
        newStatus.correctionAllowableRange = std::stoi(params[5]);
        statuses_[axisNo] = newStatus;
        spdlog::debug("Status for axis {} updated.", axisNo);
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse status parameters for axis {}: {}", axisNo, e.what());
    }
}

/**
 * @brief Retrieves the last known position of a specific axis in a thread-safe manner.
 * @param axisNo The axis number.
 * @return The cached position value. Returns -1 if axis is not found.
 */
int AxisState::getPosition(int axisNo) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find(axisNo);
    if (it != positions_.end()) {
        return it->second;
    }
    return -1;
    // Or throw an exception for clarity
}

/**
 * @brief Retrieves the last known status details of a specific axis in a thread-safe manner.
 * @param axisNo The axis number.
 * @return The cached AxisStatus structure. Returns a default-constructed structure if not found.
 */
AxisStatus AxisState::getStatusDetails(int axisNo) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = statuses_.find(axisNo);
    if (it != statuses_.end()) {
        return it->second;
    }
    return AxisStatus();
    // Return default status if not found
}
