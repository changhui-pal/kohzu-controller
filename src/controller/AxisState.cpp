#include "controller/AxisState.h"
#include <stdexcept>
#include "spdlog/spdlog.h"

/**
 * @brief Updates the current position of a specific axis in a thread-safe manner.
 * @param axis_no The axis number.
 * @param position The new position value.
 */
void AxisState::updatePosition(int axis_no, int position) {
    std::lock_guard<std::mutex> lock(mtx_);
    positions_[axis_no] = position;
    spdlog::debug("Position for axis {} updated to {}", axis_no, position);
}

/**
 * @brief Updates the detailed status of a specific axis from a protocol response.
 * @param axis_no The axis number.
 * @param params A vector of strings containing status parameters.
 */
void AxisState::updateStatus(int axis_no, const std::vector<std::string>& params) {
    if (params.size() < 6) {
        spdlog::warn("Received insufficient status parameters for axis {}. Expected at least 6, got {}.", axis_no, params.size());
        return;
    }
    std::lock_guard<std::mutex> lock(mtx_);
    AxisStatus new_status;
    try {
        new_status.driving_state = std::stoi(params[0]);
        new_status.emg_signal = std::stoi(params[1]);
        new_status.org_norg_signal = std::stoi(params[2]);
        new_status.cw_ccw_limit_signal = std::stoi(params[3]);
        new_status.soft_limit_state = std::stoi(params[4]);
        new_status.correction_allowable_range = std::stoi(params[5]);
        statuses_[axis_no] = new_status;
        spdlog::debug("Status for axis {} updated.", axis_no);
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse status parameters for axis {}: {}", axis_no, e.what());
    }
}

/**
 * @brief Retrieves the last known position of a specific axis in a thread-safe manner.
 * @param axis_no The axis number.
 * @return The cached position value. Returns -1 if axis is not found.
 */
int AxisState::getPosition(int axis_no) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = positions_.find(axis_no);
    if (it != positions_.end()) {
        return it->second;
    }
    return -1; // Or throw an exception for clarity
}

/**
 * @brief Retrieves the last known status details of a specific axis in a thread-safe manner.
 * @param axis_no The axis number.
 * @return The cached AxisStatus structure. Returns a default-constructed structure if not found.
 */
AxisStatus AxisState::getStatusDetails(int axis_no) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = statuses_.find(axis_no);
    if (it != statuses_.end()) {
        return it->second;
    }
    return AxisStatus(); // Return default status if not found
}
