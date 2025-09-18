#include "controller/KohzuController.h"
#include "spdlog/spdlog.h"
#include <iostream>
#include <sstream>
#include <memory>
#include <stdexcept>
#include <chrono>

/**
 * @brief Constructor for the KohzuController class.
 * @param protocolHandler A shared pointer to the ProtocolHandler object.
 * @param axisState A shared pointer to the AxisState object.
 */
KohzuController::KohzuController(std::shared_ptr<ProtocolHandler> protocolHandler, std::shared_ptr<AxisState> axisState)
    : protocolHandler_(protocolHandler), axisState_(axisState) {
    if (!protocolHandler_ || !axisState_) {
        throw std::invalid_argument("ProtocolHandler or AxisState object is not valid.");
    }
    spdlog::info("KohzuController object created.");
}

/**
 * @brief Starts the controller's communication logic.
 */
void KohzuController::start() {
    protocolHandler_->initialize();
    spdlog::info("Starting KohzuController.");
}

/**
 * @brief Starts periodic status monitoring for specified axes.
 * @param axesToMonitor A vector of axis numbers to monitor.
 * @param periodMs The monitoring period in milliseconds.
 */
void KohzuController::startMonitoring(const std::vector<int>& axesToMonitor, int periodMs) {
    if (isMonitoringRunning_.load()) {
        spdlog::warn("Monitoring is already running. Please stop it first.");
        return;
    }
    if (axesToMonitor.empty()) {
        spdlog::warn("No axes to monitor. Monitoring not started.");
        return;
    }

    // Set the axes to monitor and start the thread
    axesToMonitor_ = axesToMonitor;
    isMonitoringRunning_.store(true);
    monitoringThread_ = std::make_unique<std::thread>(&KohzuController::monitorThreadFunction, this, periodMs);
    
    // Log which axes are being monitored
    std::stringstream ss;
    ss << "Started periodic monitoring thread for axes: ";
    for (int axis : axesToMonitor_) {
        ss << axis << " ";
    }
    ss << "with a period of " << periodMs << "ms.";
    spdlog::info(ss.str());
}

/**
 * @brief Stops periodic status monitoring.
 */
void KohzuController::stopMonitoring() {
    if (!isMonitoringRunning_.load()) {
        spdlog::warn("Monitoring is not running.");
        return;
    }
    isMonitoringRunning_.store(false);
    if (monitoringThread_ && monitoringThread_->joinable()) {
        monitoringThread_->join();
    }
    spdlog::info("Stopped periodic monitoring thread.");
}

/**
 * @brief The function executed by the monitoring thread.
 * @param periodMs The monitoring period in milliseconds.
 */
void KohzuController::monitorThreadFunction(int periodMs) {
    while (isMonitoringRunning_.load()) {
        for (int axisNo : axesToMonitor_) {
            readPosition(axisNo);
            readStatus(axisNo);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(periodMs));
    }
}

/**
 * @brief Reads the current position of a specific axis.
 * @param axisNo The axis number.
 */
void KohzuController::readPosition(int axisNo) {
    protocolHandler_->sendCommand("RDP", axisNo, {},
        [this, axisNo](const ProtocolResponse& response) {
            if (response.status == 'C' && !response.params.empty()) {
                try {
                    int position = std::stoi(response.params[0]);
                    this->axisState_->updatePosition(axisNo, position);
                    spdlog::debug("Monitoring: Position of axis {} updated to {}.", axisNo, position);
                } catch (const std::exception& e) {
                    spdlog::error("Monitoring: Failed to parse RDP position for axis {}: {}", axisNo, e.what());
                }
            }
        });
}

/**
 * @brief Reads the detailed status of a specific axis.
 * @param axisNo The axis number.
 */
void KohzuController::readStatus(int axisNo) {
    protocolHandler_->sendCommand("STR", axisNo, {},
        [this, axisNo](const ProtocolResponse& response) {
            if (response.status == 'C' && response.params.size() >= 6) {
                this->axisState_->updateStatus(axisNo, response.params);
                spdlog::debug("Monitoring: Status of axis {} updated.", axisNo);
            }
        });
}

/**
 * @brief Commands the specified axis to move to an absolute position.
 * @param axisNo The axis number to move.
 * @param position The target absolute position.
 * @param speed The movement speed. Defaults to 0 if not provided.
 * @param responseType The response type. Defaults to 0 if not provided.
 * @param callback A function to be called when the command completes.
 */
void KohzuController::moveAbsolute(int axisNo, int position, int speed, int responseType,
                                   std::function<void(const ProtocolResponse&)> callback) {
    // According to the manual, the parameter order is: speed, position, response_type.
    std::vector<std::string> params = {
        std::to_string(speed),
        std::to_string(position),
        std::to_string(responseType)
    };
    // Use the provided callback directly
    protocolHandler_->sendCommand("APS", axisNo, params, callback);
}

/**
 * @brief Commands the specified axis to move by a relative distance.
 * @param axisNo The axis number to move.
 * @param distance The relative distance to move.
 * @param speed The movement speed. Defaults to 0 if not provided.
 * @param responseType The response type. Defaults to 0 if not provided.
 * @param callback A function to be called when the command completes.
 */
void KohzuController::moveRelative(int axisNo, int distance, int speed, int responseType,
                                   std::function<void(const ProtocolResponse&)> callback) {
    // According to the manual, the parameter order is: speed, distance, response_type.
    std::vector<std::string> params = {
        std::to_string(speed),
        std::to_string(distance),
        std::to_string(responseType)
    };
    // Use the provided callback directly
    protocolHandler_->sendCommand("RPS", axisNo, params, callback);
}