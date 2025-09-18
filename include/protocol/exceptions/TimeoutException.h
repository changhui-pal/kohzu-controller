#ifndef TIMEOUT_EXCEPTION_H
#define TIMEOUT_EXCEPTION_H

#include <stdexcept>
#include <string>

/**
 * @brief Exception class for timeout events.
 *
 * This is used when a response is not received within the specified time.
 */
class TimeoutException : public std::runtime_error {
public:
    explicit TimeoutException(const std::string& message)
        : std::runtime_error("Timeout Error: " + message) {}
};

#endif // TIMEOUT_EXCEPTION_H