#ifndef KOHZU_CONTROLLER_H
#define KOHZU_CONTROLLER_H

#include "protocol/ProtocolHandler.h"
#include "controller/AxisState.h"
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>

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

    ~KohzuController();

    /**
     * @brief Initializes the controller's communication by starting the protocol handler.
     */
    void start();

    /**
     * @brief Starts the background monitoring thread.
     * @brief The thread will initially wait until axes are added for monitoring.
     * @param initial_axes_to_monitor A vector of axis numbers to monitor initially.
     * @param period_ms The monitoring period in milliseconds.
     */
    void startMonitoring(const std::vector<int>& initialAxesToMonitor, int periodMs);

    /**
     * @brief Stops the background monitoring thread safely.
     */
    void stopMonitoring();

    /**
     * @brief Adds a single axis to the monitoring list in a thread-safe manner.
     * @brief Wakes up the monitoring thread if it was waiting.
     * @param axis_no The axis number to add.
     */
    void addAxisToMonitor(int axisNo);

    /**
     * @brief Removes a single axis from the monitoring list in a thread-safe manner.
     * @param axis_no The axis number to remove.
     */
    void removeAxisToMonitor(int axisNo);

    /**
     * @brief Commands the specified axis to move to an absolute position.
     * @param axisNo The axis number to move.
     * @param position The target absolute position.
     * @param speed The movement speed. Defaults to 0 if not provided.
     * @param responseType The response type. Defaults to 0 if not provided.
     * @param callback A function to be called when the command completes.
     */
    void moveAbsolute(int axisNo, int position, int speed = 0, int responseType = 0,
                      std::function<void(const ProtocolResponse&)> callback = nullptr);

    /**
     * @brief Commands the specified axis to move by a relative distance.
     * @param axisNo The axis number to move.
     * @param distance The relative distance to move.
     * @param speed The movement speed. Defaults to 0 if not provided.
     * @param responseType The response type. Defaults to 0 if not provided.
     * @param callback A function to be called when the command completes.
     */
    void moveRelative(int axisNo, int distance, int speed = 0, int responseType = 0,
                      std::function<void(const ProtocolResponse&)> callback = nullptr);

    /**
     * @brief Commands the specified axis to perform an origin return operation.
     * @param axisNo The axis number to move.
     * @param speed The movement speed (0-9).
     * @param responseType The response type (e.g., 0 for completion response).
     * @param callback A function to be called when the command completes.
     */
    void moveOrigin(int axisNo, int speed = 0, int responseType = 0,
                    std::function<void(const ProtocolResponse&)> callback = nullptr);

    /**
     * @brief Sets a system parameter value for a specified axis. (WSY command)
     * @param axisNo The axis number to configure.
     * @param systemNo The system parameter number.
     * @param value The value to set for the parameter.
     * @param callback A function to be called when the command completes.
     */
    void setSystem(int axisNo, int systemNo, int value,
                   std::function<void(const ProtocolResponse&)> callback = nullptr);

private:
    void monitorThreadFunction(int periodMs);
    void readPosition(int axisNo);
    void readStatus(int axisNo);
    
    std::shared_ptr<ProtocolHandler> protocolHandler_;
    std::shared_ptr<AxisState> axisState_;

    std::atomic<bool> isMonitoringRunning_{false};
    std::unique_ptr<std::thread> monitoringThread_;
    std::vector<int> axesToMonitor_;

    std::mutex monitorMutex_;
    std::condition_variable monitorCv_;
};

#endif // KOHZU_CONTROLLER_H
