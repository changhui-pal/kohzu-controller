#ifndef KOHZU_CONTROLLER_H
#define KOHZU_CONTROLLER_H

#include "protocol/ProtocolHandler.h"
#include "controller/AxisState.h"
#include <memory>
#include <string>
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

    /**
     * @brief Initializes the controller's communication by starting the protocol handler.
     */
    void start();

    /**
     * @brief Starts periodic status monitoring for specified axes.
     * @param axesToMonitor A vector of axis numbers to monitor.
     * @param periodMs The monitoring period in milliseconds.
     */
    void startMonitoring(const std::vector<int>& axesToMonitor, int periodMs);

    /**
     * @brief Stops periodic status monitoring.
     */
    void stopMonitoring();

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
    * @brief Reads the current position of a specific axis from axisState.
    * @param axisNo The axis number.
    */
    int getPosition(int axisNo);
private:
    void monitorThreadFunction(int periodMs);
    void readPosition(int axisNo);
    void readStatus(int axisNo);
    
    std::shared_ptr<ProtocolHandler> protocolHandler_;
    std::shared_ptr<AxisState> axisState_;

    std::atomic<bool> isMonitoringRunning_{false};
    std::unique_ptr<std::thread> monitoringThread_;
    std::vector<int> axesToMonitor_;
};

#endif // KOHZU_CONTROLLER_H
