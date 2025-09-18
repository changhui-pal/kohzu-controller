#ifndef CONNECTION_EXCEPTION_H
#define CONNECTION_EXCEPTION_H

#include <stdexcept>
#include <string>

/**
 * @brief Exception class for communication layer errors.
 *
 * This indicates errors related to network connectivity, such as connection failure or disconnection.
 */
class ConnectionException : public std::runtime_error {
public:
    explicit ConnectionException(const std::string& message)
        : std::runtime_error("Connection Error: " + message) {}
};

#endif // CONNECTION_EXCEPTION_H