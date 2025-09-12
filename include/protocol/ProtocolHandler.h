#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include <memory>
#include <string>
#include <map>
#include <vector>
#include "core/ICommunicationClient.h"
#include "common/ThreadSafeQueue.h"

// Struct to hold parsed response results
struct ProtocolResponse {
    char status;
    std::string command;
    int axis_no;
    std::vector<std::string> params;
};

class ProtocolHandler {
public:
    ProtocolHandler(std::shared_ptr<ICommunicationClient> client);
    ~ProtocolHandler();

    void initialize();
    void sendCommand(const std::string& command);
    ProtocolResponse waitForResponse(const std::string& command, int timeout_ms);

    // API
    /**
     * @brief Moves the motor to an absolute position.
     * @param axis_no Axis number (1-32).
     * @param position Target position (in Pulse units).
     * @param speed Speed table number (0-9).
     * @param response_type Response type (0=Complete, 1=Ready).
     */
    void moveAbsolute(int axis_no, int position, int speed, int response_type);

    /**
     * @brief Moves the motor to a relative position.
     * @param axis_no Axis number (1-32).
     * @param distance Relative distance to move (in Pulse units).
     * @param speed Speed table number (0-9).
     * @param response_type Response type (0=Complete, 1=Ready).
     */
    void moveRelative(int axis_no, int distance, int speed, int response_type);

    /**
     * @brief Reads the current position value.
     * @param axis_no Axis number (1-32).
     * @return The current position value string.
     * @throws ProtocolException Invalid parameter or response.
     * @throws TimeoutException Response timeout.
     */
    std::string readPosition(int axis_no);

private:
    void handleRead(const std::string& message);
    void handleError(const std::string& message);
    ProtocolResponse parseResponse(const std::string& response);
    void loadErrorMessages();
    void validateParameters(const std::string& command, const std::vector<std::string>& params);

    std::shared_ptr<ICommunicationClient> client_;
    ThreadSafeQueue<ProtocolResponse> responseQueue_;
    std::map<std::string, std::string> errorMessages_;
};

#endif // PROTOCOL_HANDLER_H
