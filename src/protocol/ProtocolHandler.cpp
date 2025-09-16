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
    if (!is_reading_) {
        is_reading_ = true;
        client_->asyncRead([this](const std::string& data) {
            this->handleRead(data);
        });
    }
}

/**
 * @brief Generates a key for the response_callbacks_ map.
 * @param base_command The command string.
 * @param axis_no The axis number.
 * @return A unique string key.
 */
std::string ProtocolHandler::generateResponseKey(const std::string& base_command, int axis_no) {
    if (axis_no == -1) {
        return base_command;
    }
    return base_command + std::to_string(axis_no);
}

/**
 * @brief Sends a command with an optional axis number and parameters asynchronously.
 * @param base_command The command string (e.g., "APS", "RDP", "CERR").
 * @param axis_no The axis number for the command. Use a special value (e.g., -1) if no axis number is required.
 * @param params A vector of string parameters.
 * @param callback The callback function to execute when a response is received.
 */
void ProtocolHandler::sendCommand(const std::string& base_command, int axis_no, const std::vector<std::string>& params, std::function<void(const ProtocolResponse&)> callback) {
    std::string full_command = base_command;
    if (axis_no != -1) {
        full_command += std::to_string(axis_no);
    }

    if (!params.empty()) {
        if (axis_no != -1) {
            full_command += "/";
        }
        for (size_t i = 0; i < params.size(); ++i) {
            full_command += params[i];
            if (i < params.size() - 1) {
                full_command += "/";
            }
        }
    }
    full_command += "\r\n";
    
    // Protect the map access with a lock
    std::lock_guard<std::mutex> lock(callback_mutex_);
    
    // Push the callback into the queue for the specific command and axis
    response_callbacks_[generateResponseKey(base_command, axis_no)].push(callback);
    
    // Log the full command being sent
    spdlog::info("Sending command: {}", full_command);

    client_->asyncWrite(full_command);
}

/**
 * @brief Handles the received response data.
 * @param response_data The received response string.
 */
void ProtocolHandler::handleRead(const std::string& response_data) {
    try {
        ProtocolResponse response = parseResponse(response_data);
        spdlog::info("Received response: {}", response.full_response);

        std::string response_key = generateResponseKey(response.command, response.axis_no);
        
        // Protect the map access with a lock
        std::lock_guard<std::mutex> lock(callback_mutex_);
        
        // Find the matching queue for the received response
        auto it = response_callbacks_.find(response_key);
        if (it != response_callbacks_.end()) {
            ThreadSafeQueue<std::function<void(const ProtocolResponse&)>>& queue = it->second;
            if (!queue.empty()) {
                std::function<void(const ProtocolResponse&)> callback = queue.pop();
                callback(response);
            }
            if (queue.empty()) {
                response_callbacks_.erase(it);
            }
        } else {
            // This is an unsolicited response or no matching callback was found
            spdlog::warn("No matching callback queue found for response: {}", response_data);
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
    parsed.full_response = response;
    std::string cleaned_response = response;

    // Remove carriage return and line feed from the end.
    if (!cleaned_response.empty() && cleaned_response.back() == '\n') {
        cleaned_response.pop_back();
    }
    if (!cleaned_response.empty() && cleaned_response.back() == '\r') {
        cleaned_response.pop_back();
    }
    
    if (cleaned_response.empty()) {
        throw ProtocolException("Received an empty response.");
    }

    // Use a stringstream to split the response by the tab delimiter.
    std::stringstream ss(cleaned_response);
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
        std::string command_and_axis = tokens[1];
        size_t first_digit_pos = command_and_axis.find_first_of("0123456789");
        if (first_digit_pos != std::string::npos) {
            parsed.command = command_and_axis.substr(0, first_digit_pos);
            try {
                parsed.axis_no = std::stoi(command_and_axis.substr(first_digit_pos));
            } catch (const std::exception& e) {
                throw ProtocolException("Failed to parse axis number from response: " + std::string(e.what()));
            }
        } else {
            parsed.command = command_and_axis;
            parsed.axis_no = -1; // No axis number in the response
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
