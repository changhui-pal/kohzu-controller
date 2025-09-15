#pragma once

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
     * @param io_context The Boost.Asio I/O context.
     * @param host The hostname or IP address to connect to.
     * @param port The port number to connect to.
     */
    TcpClient(boost::asio::io_context& io_context, const std::string& host, const std::string& port);
    
    // Disable copy constructor and assignment operator
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    /**
     * @brief Connects to the specified host and port.
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
    boost::asio::streambuf response_buffer_; // Buffer to handle fragmented reads
};
