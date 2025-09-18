#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include "core/ICommunicationClient.h"
#include "protocol/exceptions/ConnectionException.h"
#include "protocol/exceptions/ProtocolException.h"
#include "protocol/exceptions/TimeoutException.h"
#include "common/ThreadSafeQueue.h"
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <future>
#include <atomic>
#include <mutex>

/**
 * @struct ProtocolResponse
 * @brief Data structure for a structured protocol response.
 *
 * This structure holds the parsed components of a response string, including
 * the status, command, axis number (if present), and parameters.
 */
struct ProtocolResponse {
    char status;
    int axisNo;
    std::string command;
    std::vector<std::string> params;
    std::string fullResponse;
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
     * @brief Sends a command with an optional axis number and parameters asynchronously.
     * @param baseCommand The command string (e.g., "APS", "RDP", "CERR").
     * @param axisNo The axis number for the command. Use a special value (e.g., -1) if no axis number is required.
     * @param params A vector of string parameters.
     * @param callback The callback function to execute when a response is received.
     */
    void sendCommand(const std::string& baseCommand, int axisNo, const std::vector<std::string>& params, std::function<void(const ProtocolResponse&)> callback);

private:
    void handleRead(const std::string& responseData);
    std::string generateResponseKey(const std::string& baseCommand, int axisNo);
    ProtocolResponse parseResponse(const std::string& response);

    std::shared_ptr<ICommunicationClient> client_;
    std::map<std::string, ThreadSafeQueue<std::function<void(const ProtocolResponse&)>>> responseCallbacks_;
    std::atomic<bool> isReading_ = false;
    std::mutex callbackMutex_; // Protects the responseCallbacks_ map
};

#endif // PROTOCOL_HANDLER_H