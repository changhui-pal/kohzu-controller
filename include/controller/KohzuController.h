#pragma once

#include "protocol/ProtocolHandler.h"
#include <memory>
#include <future>

/**
 * @class KohzuController
 * @brief High-level controller for a KOHZU motion control system.
 *
 * This class provides a user-friendly API to control motors by
 * interacting with the ProtocolHandler.
 */
class KohzuController {
public:
    /**
     * @brief Constructor for the KohzuController.
     * @param handler A shared pointer to a ProtocolHandler instance.
     */
    explicit KohzuController(std::shared_ptr<ProtocolHandler> handler);

    /**
     * @brief Starts the controller, typically by initializing communication.
     */
    void start();

    /**
     * @brief Sends an absolute move command to a specific axis.
     * @param axis_no The axis number to control.
     * @param position The target absolute position.
     * @param speed The speed of the movement.
     * @param response_type The response type (0 for completion, 1 for submission).
     */
    void moveAbsolute(int axis_no, int position, int speed, int response_type);

    /**
     * @brief Sends a relative move command to a specific axis.
     * @param axis_no The axis number to control.
     * @param distance The distance to move relative to the current position.
     * @param speed The speed of the movement.
     * @param response_type The response type (0 for completion, 1 for submission).
     */
    void moveRelative(int axis_no, int distance, int speed, int response_type);

    /**
     * @brief Reads the current position of a specific axis.
     * @param axis_no The axis number to read from.
     */
    void readCurrentPosition(int axis_no);

    /**
     * @brief Reads the last error from the controller.
     */
    void readLastError();

    /**
     * @brief Starts a periodic monitoring task for a given axis position.
     * @param axis_no The axis to monitor.
     * @param interval_ms The monitoring interval in milliseconds.
     * @return A future that can be used to manage the monitoring task.
     */
    std::future<void> startPositionMonitor(int axis_no, int interval_ms);

private:
    std::shared_ptr<ProtocolHandler> protocolHandler_;
};
