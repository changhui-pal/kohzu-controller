#ifndef AXIS_STATE_H
#define AXIS_STATE_H

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
    int drivingState = 0;
    int emgSignal = 0;
    int orgNorgSignal = 0;
    int cwCcwLimitSignal = 0;
    int softLimitState = 0;
    int correctionAllowableRange = 0;
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
     * @param axisNo The axis number.
     * @param position The new position value.
     */
    void updatePosition(int axisNo, int position);

    /**
     * @brief Updates the detailed status of a specific axis from a protocol response.
     * @param axisNo The axis number.
     * @param params A vector of strings containing status parameters from the STR command.
     */
    void updateStatus(int axisNo, const std::vector<std::string>& params);

    /**
     * @brief Retrieves the last known position of a specific axis.
     * @param axisNo The axis number.
     * @return The cached position value.
     */
    int getPosition(int axisNo);

    /**
     * @brief Retrieves the last known status details of a specific axis.
     * @param axisNo The axis number.
     * @return The cached AxisStatus structure.
     */
    AxisStatus getStatusDetails(int axisNo);

private:
    std::map<int, int> positions_;
    std::map<int, AxisStatus> statuses_;
    std::mutex mutex_;
};

#endif // AXIS_STATE_H