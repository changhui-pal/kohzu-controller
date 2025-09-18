#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include "ICommunicationClient.h"
#include <boost/asio.hpp>

/**
 * @class TcpClient
 * @brief Handles TCP client communication using Boost.Asio.
 *
 * This class provides asynchronous read and write capabilities over a TCP
 * connection, abstracting the low-level socket operations.
 */
class TcpClient : public ICommunicationClient {
public:
    /**
     * @brief Constructor for TcpClient.
     * @param ioContext The Boost.Asio I/O context.
     * @param host The hostname or IP address to connect to.
     * @param port The port number to connect to.
     */
    TcpClient(boost::asio::io_context& ioContext, const std::string& host, const std::string& port);

    // Disable copy constructor and assignment operator
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    /**
     * @brief Connects to the specified host and port.
     * @param host The host address to connect to.
     * @param port The port number to connect to.
     */
    void connect(const std::string& host, const std::string& port) override;

    /**
     * @brief Asynchronously reads data from the socket.
     * @param callback The callback function to be called when data is received.
     */
    void asyncRead(std::function<void(const std::string&)> callback) override;

    /**
     * @brief Asynchronously writes data to the socket.
     * @param data The string data to be sent.
     */
    void asyncWrite(const std::string& data) override;

private:
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::streambuf responseBuffer_; // Buffer to handle fragmented reads
};

#endif // TCP_CLIENT_H