#include "controller/KohzuController.h"
#include "spdlog/spdlog.h"
#include <stdexcept>
#include <chrono>
#include <vector>
#include <string>

// A constant to represent a command without an axis number
static const int kNoAxis = -1;

KohzuController::KohzuController(std::shared_ptr<ProtocolHandler> handler)
    : protocolHandler_(handler) {
    if (!protocolHandler_) {
        throw std::invalid_argument("ProtocolHandler is not valid.");
    }
    spdlog::info("KohzuController object created.");
}

void KohzuController::start() {
    spdlog::info("Starting KohzuController.");
    protocolHandler_->initialize();
    // TODO: Add controller initialization logic (e.g., homing)
}

void KohzuController::moveAbsolute(int axis_no, int position, int speed, int response_type) {
    std::vector<std::string> params = {
        std::to_string(position),
        std::to_string(speed),
        std::to_string(response_type)
    };
    
    if (response_type == 0) {
        protocolHandler_->sendCommand(
            "APS",
            axis_no,
            params,
            [this, axis_no](const ProtocolResponse& response) {
                spdlog::info("Axis {} absolute move completed.", axis_no);
            }
        );
    } else {
        protocolHandler_->sendCommand(
            "APS",
            axis_no,
            params,
            [this, axis_no](const ProtocolResponse& response) {
                spdlog::info("Axis {} absolute move started.", axis_no);
            }
        );
    }
}

void KohzuController::moveRelative(int axis_no, int distance, int speed, int response_type) {
    std::vector<std::string> params = {
        std::to_string(distance),
        std::to_string(speed),
        std::to_string(response_type)
    };
    
    if (response_type == 0) {
        protocolHandler_->sendCommand(
            "RPS",
            axis_no,
            params,
            [this, axis_no](const ProtocolResponse& response) {
                spdlog::info("Axis {} relative move completed.", axis_no);
            }
        );
    } else {
        protocolHandler_->sendCommand(
            "RPS",
            axis_no,
            params,
            [this, axis_no](const ProtocolResponse& response) {
                spdlog::info("Axis {} relative move started.", axis_no);
            }
        );
    }
}

void KohzuController::readCurrentPosition(int axis_no) {
    protocolHandler_->sendCommand(
        "RDP",
        axis_no,
        {},
        [axis_no](const ProtocolResponse& response) {
            if (response.status == 'C' && !response.params.empty()) {
                spdlog::info("Current position of axis {}: {}", axis_no, response.params[0]);
            } else {
                spdlog::error("Invalid response for RDP command on axis {}.", axis_no);
            }
        }
    );
}

void KohzuController::readLastError() {
    protocolHandler_->sendCommand(
        "CERR",
        kNoAxis,
        {},
        [this](const ProtocolResponse& response) {
            if (response.status == 'E' && !response.params.empty()) {
                spdlog::error("Controller error code: {}", response.params[0]);
            } else {
                spdlog::info("No controller error reported.");
            }
        }
    );
}

std::future<void> KohzuController::startPositionMonitor(int axis_no, int interval_ms) {
    // TODO: Implement logic to periodically send RDP commands and update the position via a callback.
    // This can be done using a Boost.Asio timer or a separate thread with a sleep function.
    spdlog::info("Starting position monitoring for axis {} (feature to be implemented).", axis_no);
    return {};
}
