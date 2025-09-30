#include "controller/KohzuController.h"
#include "spdlog/spdlog.h"
#include <memory>
#include <stdexcept>
#include <chrono>
#include <algorithm>

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
 * @brief Destructor for the KohzuController class.
 * @brief Ensures the monitoring thread is properly stopped and joined.
 */
KohzuController::~KohzuController() {
    stopMonitoring();
}

/**
 * @brief Starts the controller's communication logic.
 */
void KohzuController::start() {
    protocolHandler_->initialize();
    spdlog::info("Starting KohzuController.");
}

/**
 * @brief Starts the background monitoring thread.
 * @param initialAxesToMonitor A vector of axis numbers to monitor initially.
 * @param periodMs The monitoring period in milliseconds.
 */
void KohzuController::startMonitoring(const std::vector<int>& initialAxesToMonitor, int periodMs) {
    if (isMonitoringRunning_.load()) {
        spdlog::warn("Monitoring thread is already running.");
        return;
    }

    axesToMonitor_ = initialAxesToMonitor;
    isMonitoringRunning_.store(true);
    monitoringThread_ = std::make_unique<std::thread>(&KohzuController::monitorThreadFunction, this, periodMs);
    spdlog::info("Started periodic monitoring thread.");
}

/**
 * @brief Stops the background monitoring thread safely.
 */
void KohzuController::stopMonitoring() {
    if (!isMonitoringRunning_.load()) {
        return;
    }
    isMonitoringRunning_.store(false);
    monitorCv_.notify_one(); // Wake up the thread if it's waiting
    if (monitoringThread_ && monitoringThread_->joinable()) {
        monitoringThread_->join();
    }
    monitoringThread_.reset();
    spdlog::info("Stopped periodic monitoring thread.");
}

/**
 * @brief Adds a single axis to the monitoring list in a thread-safe manner.
 * @param axisNo The axis number to add.
 */
void KohzuController::addAxisToMonitor(int axisNo) {
    std::lock_guard<std::mutex> lock(monitorMutex_);
    // Prevent duplicates
    if (std::find(axesToMonitor_.begin(), axesToMonitor_.end(), axisNo) == axesToMonitor_.end()) {
        axesToMonitor_.push_back(axisNo);
        spdlog::debug("Added axis {} to monitoring list.", axisNo);
    }
    monitorCv_.notify_one(); // Wake up the thread
}

/**
 * @brief Removes a single axis from the monitoring list in a thread-safe manner.
 * @param axisNo The axis number to remove.
 */
void KohzuController::removeAxisToMonitor(int axisNo) {
    std::lock_guard<std::mutex> lock(monitorMutex_);
    auto it = std::remove(axesToMonitor_.begin(), axesToMonitor_.end(), axisNo);
    if (it != axesToMonitor_.end()) {
        axesToMonitor_.erase(it, axesToMonitor_.end());
        spdlog::debug("Removed axis {} from monitoring list.", axisNo);
    }
}

/**
 * @brief The function executed by the monitoring thread.
 * @brief Waits until axes are available or until stopped.
 * @param periodMs The monitoring period in milliseconds.
 */
void KohzuController::monitorThreadFunction(int periodMs) {
    while (isMonitoringRunning_.load()) {
        std::vector<int> current_axes;
        {
            std::unique_lock<std::mutex> lock(monitorMutex_);
            monitorCv_.wait(lock, [this] {
                return !isMonitoringRunning_.load() || !axesToMonitor_.empty();
            });

            if (!isMonitoringRunning_.load()) {
                break; // Exit if stopped while waiting
            }

            // Copy the axes to a local variable to minimize lock time
            current_axes = axesToMonitor_;
        }

        // Perform monitoring outside the lock
        for (int axis_no : current_axes) {
            readPosition(axis_no);
            readStatus(axis_no);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(periodMs));
    }
}

/**
 * @brief Reads the current position of a specific axis and update axisState.
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
 * @brief Reads the detailed status of a specific axis and update axisState.
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

    /**
     * @brief Commands the specified axis to perform an origin return operation.
     * @param axisNo The axis number to move.
     * @param speed The movement speed (0-9).
     * @param responseType The response type (e.g., 0 for completion response).
     * @param callback A function to be called when the command completes.
     */
void KohzuController::moveOrigin(int axisNo, int speed, int responseType,
                                 std::function<void(const ProtocolResponse&)> callback) {
    std::vector<std::string> params = {
        std::to_string(speed),
        std::to_string(responseType)
    };
    protocolHandler_->sendCommand("ORG", axisNo, params, callback);
}

/**
     * @brief Sets a system parameter value for a specified axis. (WSY command)
     * @param axisNo The axis number to configure.
     * @param systemNo The system parameter number.
     * @param value The value to set for the parameter.
     * @param callback A function to be called when the command completes.
     */
void KohzuController::setSystem(int axisNo, int systemNo, int value,
                                std::function<void(const ProtocolResponse&)> callback) {
    std::vector<std::string> params = {
        std::to_string(systemNo),
        std::to_string(value)
    };
    protocolHandler_->sendCommand("WSY", axisNo, params, callback);
}
