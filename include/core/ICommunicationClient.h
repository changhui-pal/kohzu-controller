#ifndef I_COMMUNICATION_CLIENT_H
#define I_COMMUNICATION_CLIENT_H

#include <string>
#include <functional>

/**
 * @interface ICommunicationClient
 * @brief Abstract interface for a communication client.
 *
 * This interface includes asynchronous data send and receive functions to
 * decouple the communication layer.
 */
class ICommunicationClient {
public:
    virtual ~ICommunicationClient() = default;

    /**
     * @brief Method to connect to the controller.
     * @param host The host address to connect to.
     * @param port The port number to connect to.
     */
    virtual void connect(const std::string& host, const std::string& port) = 0;

    /**
     * @brief Method to send data asynchronously.
     * @param data The string data to be sent.
     */
    virtual void asyncWrite(const std::string& data) = 0;

    /**
     * @brief Method to start receiving data asynchronously.
     * @param callback The callback function to be called upon completion of receiving.
     */
    virtual void asyncRead(std::function<void(const std::string&)> callback) = 0;
};

#endif // I_COMMUNICATION_CLIENT_H