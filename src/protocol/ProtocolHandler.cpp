#include "protocol/ProtocolHandler.h"
#include "protocol/exceptions/ConnectionException.h"
#include "protocol/exceptions/ProtocolException.h"
#include "protocol/exceptions/TimeoutException.h"
#include "spdlog/spdlog.h"
#include <sstream>
#include <vector>
#include <stdexcept>
#include <regex>

ProtocolHandler::ProtocolHandler(std::shared_ptr<ICommunicationClient> client)
    : client_(client) {
    if (!client_) {
        throw std::invalid_argument("ICommunicationClient cannot be null.");
    }
    loadErrorMessages();
    spdlog::info("ProtocolHandler 객체가 생성되었습니다.");
}

ProtocolHandler::~ProtocolHandler() {
    spdlog::info("ProtocolHandler 객체가 소멸되었습니다.");
}

void ProtocolHandler::initialize() {
    client_->setReadCallback(std::bind(&ProtocolHandler::handleRead, this, std::placeholders::_1));
    client_->setErrorCallback(std::bind(&ProtocolHandler::handleError, this, std::placeholders::_1));
}

void ProtocolHandler::sendCommand(const std::string& command) {
    client_->write(command + "\r\n");
}

ProtocolResponse ProtocolHandler::waitForResponse(const std::string& command, int timeout_ms) {
    ProtocolResponse response;
    if (!responseQueue_.try_pop(response, timeout_ms)) {
        throw TimeoutException("Response timeout for command: " + command);
    }
    return response;
}

void ProtocolHandler::handleRead(const std::string& message) {
    try {
        ProtocolResponse response = parseResponse(message);
        responseQueue_.push(response);
    } catch (const ProtocolException& e) {
        spdlog::error("Protocol error during message handling: {}", e.what());
    }
}

void ProtocolHandler::handleError(const std::string& message) {
    spdlog::error("Communication error: {}", message);
    throw ConnectionException(message);
}

ProtocolResponse ProtocolHandler::parseResponse(const std::string& response_str) {
    ProtocolResponse response;
    std::string clean_response = response_str;
    if (!clean_response.empty() && clean_response.back() == '\r') {
        clean_response.pop_back();
    }

    std::stringstream ss(clean_response);
    std::string segment;
    std::vector<std::string> segments;

    while (std::getline(ss, segment, '\t')) {
        segments.push_back(segment);
    }

    if (segments.empty()) {
        throw ProtocolException("Empty or invalid response received.");
    }

    response.status = segments[0][0];
    response.command = "";
    response.axis_no = -1;

    if (segments.size() > 1) {
        std::string command_segment = segments[1];
        std::smatch match;
        std::regex command_regex("^([A-Z]+)([0-9]*)$");

        if (std::regex_match(command_segment, match, command_regex)) {
            response.command = match[1];
            if (match.size() > 2 && !match[2].str().empty()) {
                response.axis_no = std::stoi(match[2]);
            }
        }
    }

    if (segments.size() > 2) {
        for (size_t i = 2; i < segments.size(); ++i) {
            response.params.push_back(segments[i]);
        }
    }

    if (response.status == 'E' || response.status == 'W') {
        if (!response.params.empty()) {
            std::string code = response.params[0];
            if (errorMessages_.count(code)) {
                spdlog::error("Controller response error: {} - {}", code, errorMessages_[code]);
            }
        }
        if (response.status == 'E') {
            throw ProtocolException("Controller responded with an error.");
        }
    }

    return response;
}

void ProtocolHandler::loadErrorMessages() {
    errorMessages_["100"] = "Total number of parameters is incorrect.";
    errorMessages_["101"] = "Parameter type or value is incorrect.";
    errorMessages_["102"] = "Command is undefined.";
    // TODO: Add all error/warning codes from the manual.
}

void ProtocolHandler::validateParameters(const std::string& command, const std::vector<std::string>& params) {
    // This function validates the parameters for each command.
    // The validation logic for APS, RPS, and RDP, as specified in the design document, should be implemented here.
}

void ProtocolHandler::moveAbsolute(int axis_no, int position, int speed, int response_type) {
    // TODO: Add parameter validation logic
    std::string cmd = "APS" + std::to_string(axis_no) + "/" + std::to_string(position) + "/" + std::to_string(speed) + "/" + std::to_string(response_type);
    sendCommand(cmd);
}

void ProtocolHandler::moveRelative(int axis_no, int distance, int speed, int response_type) {
    // TODO: Add parameter validation logic
    std::string cmd = "RPS" + std::to_string(axis_no) + "/" + std::to_string(distance) + "/" + std::to_string(speed) + "/" + std::to_string(response_type);
    sendCommand(cmd);
}

std::string ProtocolHandler::readPosition(int axis_no) {
    // TODO: Add parameter validation logic
    std::string cmd = "RDP" + std::to_string(axis_no);
    sendCommand(cmd);

    ProtocolResponse response = waitForResponse("RDP", 5000); // 5 sec timeout
    if (response.status == 'C' && response.command == "RDP" && response.axis_no == axis_no && !response.params.empty()) {
        return response.params[0];
    }
    throw ProtocolException("Invalid response for RDP command.");
}
