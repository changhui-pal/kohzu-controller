#include "protocol/ProtocolHandler.h"
#include "spdlog/spdlog.h"
#include <stdexcept>
#include <sstream>
#include <boost/asio.hpp>
#include <atomic>

/**
 * @brief Constructor for the ProtocolHandler class.
 * @param client The communication client object.
 */
ProtocolHandler::ProtocolHandler(std::shared_ptr<ICommunicationClient> client)
    : client_(client) {
    if (!client_) {
        throw std::invalid_argument("ICommunicationClient object is not valid.");
    }
    spdlog::info("ProtocolHandler object created.");
}

/**
 * @brief Initializes the protocol handler and starts the asynchronous read operation.
 */
void ProtocolHandler::initialize() {
    if (!isReading_) {
        isReading_ = true;
        client_->asyncRead([this](const std::string& data) {
            this->handleRead(data);
        });
    }
}

/**
 * @brief Generates a key for the responseCallbacks_ map.
 * @param baseCommand The command string.
 * @param axisNo The axis number.
 * @return A unique string key.
 */
std::string ProtocolHandler::generateResponseKey(const std::string& baseCommand, int axisNo) {
    if (axisNo == -1) {
        return baseCommand;
    }
    return baseCommand + std::to_string(axisNo);
}

/**
 * @brief Sends a command with an optional axis number and parameters asynchronously.
 * @param baseCommand The command string (e.g., "APS", "RDP", "CERR").
 * @param axisNo The axis number for the command. Use a special value (e.g., -1) if no axis number is required.
 * @param params A vector of string parameters.
 * @param callback The callback function to execute when a response is received.
 */
void ProtocolHandler::sendCommand(const std::string& baseCommand, int axisNo, const std::vector<std::string>& params, std::function<void(const ProtocolResponse&)> callback) {
    std::string fullCommand = baseCommand;
    if (axisNo != -1) {
        fullCommand += std::to_string(axisNo);
    }

    if (!params.empty()) {
        if (axisNo != -1) {
            fullCommand += "/";
        }
        for (size_t i = 0; i < params.size(); ++i) {
            fullCommand += params[i];
            if (i < params.size() - 1) {
                fullCommand += "/";
            }
        }
    }
    fullCommand += "\r\n";
    // Protect the map access with a lock
    std::lock_guard<std::mutex> lock(callbackMutex_);
    // Push the callback into the queue for the specific command and axis
    responseCallbacks_[generateResponseKey(baseCommand, axisNo)].push(callback);
    // Log the full command being sent
    spdlog::info("Sending command: {}", fullCommand);

    client_->asyncWrite(fullCommand);
}

/**
 * @brief Handles the received response data.
 * @param responseData The received response string.
 */
void ProtocolHandler::handleRead(const std::string& responseData) {
    try {
        ProtocolResponse response = parseResponse(responseData);
        spdlog::info("Received response: {}", response.fullResponse);

        std::string responseKey = generateResponseKey(response.command, response.axisNo);
        
        // Protect the map access with a lock
        std::lock_guard<std::mutex> lock(callbackMutex_);
        // Find the matching queue for the received response
        auto it = responseCallbacks_.find(responseKey);
        if (it != responseCallbacks_.end()) {
            ThreadSafeQueue<std::function<void(const ProtocolResponse&)>>& queue = it->second;
            if (!queue.empty()) {
                std::function<void(const ProtocolResponse&)> callback = queue.pop();
                callback(response);
            }
            if (queue.empty()) {
                responseCallbacks_.erase(it);
            }
        } else {
            // This is an unsolicited response or no matching callback was found
            spdlog::warn("No matching callback queue found for response: {}", responseData);
        }

    } catch (const ProtocolException& e) {
        spdlog::error("Protocol error: {}", e.what());
    }

    client_->asyncRead([this](const std::string& data) {
        this->handleRead(data);
    });
}

/**
 * @brief Parses the response string into a ProtocolResponse struct based on the provided manual.
 * @param response The response string to parse.
 * @return The parsed ProtocolResponse object.
 */
ProtocolResponse ProtocolHandler::parseResponse(const std::string& response) {
    ProtocolResponse parsed;
    parsed.fullResponse = response;
    std::string cleanedResponse = response;
    // Remove carriage return and line feed from the end.
    if (!cleanedResponse.empty() && cleanedResponse.back() == '\n') {
        cleanedResponse.pop_back();
    }
    if (!cleanedResponse.empty() && cleanedResponse.back() == '\r') {
        cleanedResponse.pop_back();
    }
    
    if (cleanedResponse.empty()) {
        throw ProtocolException("Received an empty response.");
    }

    // Use a stringstream to split the response by the tab delimiter.
    std::stringstream ss(cleanedResponse);
    std::vector<std::string> tokens;
    std::string token;
    while (std::getline(ss, token, '\t')) {
        tokens.push_back(token);
    }

    if (tokens.empty()) {
        throw ProtocolException("Invalid response format: No fields found.");
    }

    // 1. Parse Status (first field)
    parsed.status = tokens[0][0];
    // 2. Parse Command and Axis No. (second field)
    if (tokens.size() > 1) {
        std::string commandAndAxis = tokens[1];
        size_t firstDigitPos = commandAndAxis.find_first_of("0123456789");
        if (firstDigitPos != std::string::npos) {
            parsed.command = commandAndAxis.substr(0, firstDigitPos);
            try {
                parsed.axisNo = std::stoi(commandAndAxis.substr(firstDigitPos));
            } catch (const std::exception& e) {
                throw ProtocolException("Failed to parse axis number from response: " + std::string(e.what()));
            }
        } else {
            parsed.command = commandAndAxis;
            parsed.axisNo = -1; // No axis number in the response
        }
    } else {
        throw ProtocolException("Invalid response format: Missing command field.");
    }

    // 3. Parse Parameters (remaining fields)
    for (size_t i = 2; i < tokens.size(); ++i) {
        parsed.params.push_back(tokens[i]);
    }
    
    return parsed;
}