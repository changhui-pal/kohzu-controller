#ifndef KOHZU_CONTROLLER_H
#define KOHZU_CONTROLLER_H

#include <string>
#include <memory>
#include "protocol/ProtocolHandler.h"
#include "spdlog/spdlog.h"
#include <stdexcept>

class KohzuController {
public:
    /**
     * @brief KohzuController constructor. Receives a ProtocolHandler object through dependency injection.
     * @param protocolHandler A shared pointer to the ProtocolHandler object.
     */
    KohzuController(std::shared_ptr<ProtocolHandler> protocolHandler);

    /**
     * @brief Starts the controller logic.
     */
    void start();

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
     * @throws ConnectionException Communication connection error.
     * @throws ProtocolException Protocol error.
     * @throws TimeoutException Response timeout.
     */
    std::string readCurrentPosition(int axis_no);

private:
    std::shared_ptr<ProtocolHandler> protocolHandler_;
};

#endif // KOHZU_CONTROLLER_H
