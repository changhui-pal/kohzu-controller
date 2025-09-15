#pragma once

#include "protocol/ProtocolHandler.h"
#include "controller/AxisState.h" // Include the new AxisState header
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

/**
 * @class KohzuController
 * @brief Handles the high-level control logic for the Kohzu ARIES/LYNX motion controller.
 *
 * This class translates user commands into the specific communication protocol
 * required by the controller and manages the asynchronous command flow.
 */
class KohzuController {
public:
    /**
     * @brief Constructs a KohzuController object.
     * @param protocolHandler A shared pointer to the ProtocolHandler instance.
     * @param axisState A shared pointer to the AxisState instance for status management.
     */
    explicit KohzuController(std::shared_ptr<ProtocolHandler> protocolHandler, std::shared_ptr<AxisState> axisState);

    /**
     * @brief Initializes the controller's communication by starting the protocol handler.
     */
    void start();

    /**
     * @brief Starts periodic status monitoring for specified axes.
     * @param axes_to_monitor A vector of axis numbers to monitor.
     * @param period_ms The monitoring period in milliseconds.
     */
    void startMonitoring(const std::vector<int>& axes_to_monitor, int period_ms);

    /**
     * @brief Stops periodic status monitoring.
     */
    void stopMonitoring();

    /**
     * @brief Commands the specified axis to move to an absolute position.
     * @param axis_no The axis number to move.
     * @param position The target absolute position.
     * @param speed The movement speed. Defaults to 0 if not provided.
     * @param response_type The response type. Defaults to 0 if not provided.
     */
    void moveAbsolute(int axis_no, int position, int speed = 0, int response_type = 0);

    /**
     * @brief Commands the specified axis to move by a relative distance.
     * @param axis_no The axis number to move.
     * @param distance The relative distance to move.
     * @param speed The movement speed. Defaults to 0 if not provided.
     * @param response_type The response type. Defaults to 0 if not provided.
     */
    void moveRelative(int axis_no, int distance, int speed = 0, int response_type = 0);

private:
    void monitorThreadFunction(int period_ms);
    
    std::shared_ptr<ProtocolHandler> protocolHandler_;
    std::shared_ptr<AxisState> axisState_;

    std::atomic<bool> monitoring_running_{false};
    std::unique_ptr<std::thread> monitoring_thread_;
    std::vector<int> axes_to_monitor_;
};
