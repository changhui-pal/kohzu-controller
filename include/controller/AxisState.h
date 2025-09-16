#pragma once

#include <mutex>
#include <map>
#include <string>
#include <vector>

/**
 * @struct AxisStatus
 * @brief Represents the detailed status of a single axis.
 *
 * This structure holds various status flags and values returned by the STR command.
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
 * @class AxisState
 * @brief Manages the state (position, status) of all axes in a thread-safe manner.
 *
 * This class provides a centralized, shared data store for axis information.
 * A mutex is used to ensure that data can be accessed and modified safely
 * from multiple threads (e.g., the monitoring thread and the main command thread).
 */
class AxisState {
public:
    /**
     * @brief Updates the current position of a specific axis.
     * @param axis_no The axis number.
     * @param position The new position value.
     */
    void updatePosition(int axis_no, int position);

    /**
     * @brief Updates the detailed status of a specific axis from a protocol response.
     * @param axis_no The axis number.
     * @param params A vector of strings containing status parameters from the STR command.
     */
    void updateStatus(int axis_no, const std::vector<std::string>& params);

    /**
     * @brief Retrieves the last known position of a specific axis.
     * @param axis_no The axis number.
     * @return The cached position value.
     */
    int getPosition(int axis_no);

    /**
     * @brief Retrieves the last known status details of a specific axis.
     * @param axis_no The axis number.
     * @return The cached AxisStatus structure.
     */
    AxisStatus getStatusDetails(int axis_no);

private:
    std::map<int, int> positions_;
    std::map<int, AxisStatus> statuses_;
    std::mutex mtx_;
};
