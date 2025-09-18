#ifndef PROTOCOL_EXCEPTION_H
#define PROTOCOL_EXCEPTION_H

#include <stdexcept>
#include <string>

/**
 * @brief Exception class for protocol layer errors.
 *
 * This indicates errors like invalid command parameters or response parsing failures.
 */
class ProtocolException : public std::runtime_error {
public:
    explicit ProtocolException(const std::string& message)
        : std::runtime_error("Protocol Error: " + message) {}
};

#endif // PROTOCOL_EXCEPTION_H