#ifndef I_COMMUNICATION_CLIENT_H
#define I_COMMUNICATION_CLIENT_H

#include <string>
#include <functional>

/**
 * @brief Asynchronous communication client interface.
 *
 * This interface provides an abstraction for various communication methods
 * like TCP/IP or RS-232C, ensuring that the higher-level ProtocolHandler
 * is not dependent on a specific communication implementation.
 */
class ICommunicationClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using ErrorCallback = std::function<void(const std::string&)>;

    virtual ~ICommunicationClient() = default;

    /**
     * @brief Attempts to connect to the server.
     * @param host The IP address or hostname of the server.
     * @param port The port number of the server.
     */
    virtual void connect(const std::string& host, const std::string& port) = 0;

    /**
     * @brief Disconnects from the server.
     */
    virtual void disconnect() = 0;

    /**
     * @brief Asynchronously sends data to the server.
     * @param message The data string to send.
     */
    virtual void write(const std::string& message) = 0;

    /**
     * @brief Registers a callback function to be called upon receiving a message.
     * @param callback The callback function to receive the message.
     */
    virtual void setReadCallback(MessageCallback callback) = 0;

    /**
     * @brief Registers a callback function to be called upon an error.
     * @param callback The callback function to receive the error message.
     */
    virtual void setErrorCallback(ErrorCallback callback) = 0;
};

#endif // I_COMMUNICATION_CLIENT_H
