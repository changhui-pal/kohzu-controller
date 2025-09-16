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
 * @param axes_to_monitor A vector of axis numbers to monitor.
 * @param period_ms The monitoring period in milliseconds.
 */
void KohzuController::startMonitoring(const std::vector<int>& axes_to_monitor, int period_ms) {
    if (monitoring_running_.load()) {
        spdlog::warn("Monitoring is already running. Please stop it first.");
        return;
    }
    if (axes_to_monitor.empty()) {
        spdlog::warn("No axes to monitor. Monitoring not started.");
        return;
    }

    // Set the axes to monitor and start the thread
    axes_to_monitor_ = axes_to_monitor;
    monitoring_running_.store(true);
    monitoring_thread_ = std::make_unique<std::thread>(&KohzuController::monitorThreadFunction, this, period_ms);
    
    // Log which axes are being monitored
    std::stringstream ss;
    ss << "Started periodic monitoring thread for axes: ";
    for (int axis : axes_to_monitor_) {
        ss << axis << " ";
    }
    ss << "with a period of " << period_ms << "ms.";
    spdlog::info(ss.str());
}

/**
 * @brief Stops periodic status monitoring.
 */
void KohzuController::stopMonitoring() {
    if (!monitoring_running_.load()) {
        spdlog::warn("Monitoring is not running.");
        return;
    }
    monitoring_running_.store(false);
    if (monitoring_thread_ && monitoring_thread_->joinable()) {
        monitoring_thread_->join();
    }
    spdlog::info("Stopped periodic monitoring thread.");
}

/**
 * @brief The function executed by the monitoring thread.
 * @param period_ms The monitoring period in milliseconds.
 */
void KohzuController::monitorThreadFunction(int period_ms) {
    while (monitoring_running_.load()) {
        for (int axis_no : axes_to_monitor_) {
            readPosition(axis_no);
            readStatus(axis_no);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
    }
}

/**
 * @brief Reads the current position of a specific axis.
 * @param axis_no The axis number.
 */
void KohzuController::readPosition(int axis_no) {
    protocolHandler_->sendCommand("RDP", axis_no, {},
        [this, axis_no](const ProtocolResponse& response) {
            if (response.status == 'C' && !response.params.empty()) {
                try {
                    int position = std::stoi(response.params[0]);
                    this->axisState_->updatePosition(axis_no, position);
                    spdlog::debug("Monitoring: Position of axis {} updated to {}.", axis_no, position);
                } catch (const std::exception& e) {
                    spdlog::error("Monitoring: Failed to parse RDP position for axis {}: {}", axis_no, e.what());
                }
            }
        });
}

/**
 * @brief Reads the detailed status of a specific axis.
 * @param axis_no The axis number.
 */
void KohzuController::readStatus(int axis_no) {
    protocolHandler_->sendCommand("STR", axis_no, {},
        [this, axis_no](const ProtocolResponse& response) {
            if (response.status == 'C' && response.params.size() >= 6) {
                this->axisState_->updateStatus(axis_no, response.params);
                spdlog::debug("Monitoring: Status of axis {} updated.", axis_no);
            }
        });
}

/**
 * @brief Commands the specified axis to move to an absolute position.
 * @param axis_no The axis number to move.
 * @param position The target absolute position.
 * @param speed The movement speed. Defaults to 0 if not provided.
 * @param response_type The response type. Defaults to 0 if not provided.
 * @param callback A function to be called when the command completes.
 */
void KohzuController::moveAbsolute(int axis_no, int position, int speed, int response_type,
                                   std::function<void(const ProtocolResponse&)> callback) {
    // According to the manual, the parameter order is: speed, position, response_type.
    std::vector<std::string> params = {
        std::to_string(speed),
        std::to_string(position),
        std::to_string(response_type)
    };

    // Use the provided callback directly
    protocolHandler_->sendCommand("APS", axis_no, params, callback);
}

/**
 * @brief Commands the specified axis to move by a relative distance.
 * @param axis_no The axis number to move.
 * @param distance The relative distance to move.
 * @param speed The movement speed. Defaults to 0 if not provided.
 * @param response_type The response type. Defaults to 0 if not provided.
 * @param callback A function to be called when the command completes.
 */
void KohzuController::moveRelative(int axis_no, int distance, int speed, int response_type,
                                   std::function<void(const ProtocolResponse&)> callback) {
    // According to the manual, the parameter order is: speed, distance, response_type.
    std::vector<std::string> params = {
        std::to_string(speed),
        std::to_string(distance),
        std::to_string(response_type)
    };

    // Use the provided callback directly
    protocolHandler_->sendCommand("RPS", axis_no, params, callback);
}
