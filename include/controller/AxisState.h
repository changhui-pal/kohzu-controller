#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>

/**
 * @struct AxisStatus
 * @brief Represents the detailed status flags from the STR command.
 */
struct AxisStatus {
    int driving_state = 0;
    int emg_signal = 0;
    int org_norg_signal = 0;
    int cw_ccw_limit_signal = 0;
    int soft_limit_state = 0;
    int correction_allowable_range = 0;
};

/**
 * @struct AxisData
 * @brief Represents the state data for a single axis.
 */
struct AxisData {
    int position = 0;
    AxisStatus status;
    std::chrono::steady_clock::time_point last_updated_time;
};

/**
 * @class AxisState
 * @brief Manages the state of all axes, providing a thread-safe way to access
 * position and status information.
 */
class AxisState {
public:
    // Update the position of a specific axis
    void updatePosition(int axis_no, int position);

    // Update the status of a specific axis from STR command parameters
    void updateStatus(int axis_no, const std::vector<std::string>& params);

    // Get the position of a specific axis
    int getPosition(int axis_no);

    // Get the status of a specific axis
    AxisStatus getStatusDetails(int axis_no);

private:
    std::map<int, AxisData> axes_data_;
    std::mutex mtx_;
};
