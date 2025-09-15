#pragma once

#include "core/ICommunicationClient.h"
#include "common/ThreadSafeQueue.h"
#include "protocol/exceptions/ConnectionException.h"
#include "protocol/exceptions/ProtocolException.h"
#include "protocol/exceptions/TimeoutException.h"
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <future>
#include <atomic>

/**
 * @struct ProtocolResponse
 * @brief Data structure for a structured protocol response.
 *
 * This structure holds the parsed components of a response string, including
 * the status, command, axis number (if present), and parameters.
 */
struct ProtocolResponse {
    char status;
    int axis_no;
    std::string command;
    std::vector<std::string> params;
    std::string full_response;
};

/**
 * @class ProtocolHandler
 * @brief Handles the communication protocol with the KOHZU controller.
 *
 * This class is responsible for formatting commands according to the protocol
 * specification and parsing incoming responses to a structured format. It also
 * manages asynchronous command execution using callbacks.
 */
class ProtocolHandler {
public:
    /**
     * @brief Constructor for the ProtocolHandler class.
     * @param client A shared pointer to the communication client object.
     */
    explicit ProtocolHandler(std::shared_ptr<ICommunicationClient> client);

    /**
     * @brief Initializes the protocol handler.
     */
    void initialize();

    /**
     * @brief Sends a command with axis number and parameters asynchronously.
     * @param base_command The command string (e.g., "APS").
     * @param axis_no The axis number for the command.
     * @param params The parameter string (e.g., "100/1000/0").
     * @param callback The callback function to execute when a response is received.
     */
    void sendCommand(const std::string& base_command, int axis_no, const std::string& params, std::function<void(const ProtocolResponse&)> callback);

    /**
     * @brief Sends a command with only axis number asynchronously.
     * @param base_command The command string (e.g., "RDP").
     * @param axis_no The axis number for the command.
     * @param callback The callback function to execute when a response is received.
     */
    void sendCommand(const std::string& base_command, int axis_no, std::function<void(const ProtocolResponse&)> callback);

    /**
     * @brief Sends a command with no axis number or parameters asynchronously.
     * @param base_command The command string (e.g., "CERR").
     * @param callback The callback function to execute when a response is received.
     */
    void sendCommand(const std::string& base_command, std::function<void(const ProtocolResponse&)> callback);

private:
    void handleRead(const std::string& response_data);
    ProtocolResponse parseResponse(const std::string& response);
    // Helper function to generate a consistent key for the callback map.
    std::string generateResponseKey(const std::string& base_command, int axis_no);

    std::shared_ptr<ICommunicationClient> client_;
    // Maps callbacks to a unique key (command/axis_no or just command)
    std::map<std::string, std::function<void(const ProtocolResponse&)>> response_callbacks_;
    std::atomic<bool> is_reading_ = false;
};
