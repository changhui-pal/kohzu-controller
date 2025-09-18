#include "core/TcpClient.h"
#include "spdlog/spdlog.h"
#include "protocol/exceptions/ConnectionException.h"
#include <iostream>
#include <boost/asio.hpp>

/**
 * @brief Constructor for TcpClient.
 * @param ioContext The Boost.Asio I/O context.
 * @param host The hostname or IP address to connect to.
 * @param port The port number to connect to.
 */
TcpClient::TcpClient(boost::asio::io_context& ioContext, const std::string& host, const std::string& port)
    : socket_(ioContext),
      resolver_(ioContext) {
    spdlog::info("TcpClient object created: {}:{}", host, port);
}

/**
 * @brief Connects to the specified host and port.
 * @param host The hostname or IP address.
 * @param port The port number.
 */
void TcpClient::connect(const std::string& host, const std::string& port) {
    try {
        boost::asio::connect(socket_, resolver_.resolve(host, port));
        spdlog::info("Successfully connected to the server: {}:{}", host, port);
    } catch (const boost::system::system_error& e) {
        throw ConnectionException("Connection failed: " + std::string(e.what()));
    }
}

/**
 * @brief Asynchronously reads data from the socket.
 * @param callback The callback function to be called when data is received.
 */
void TcpClient::asyncRead(std::function<void(const std::string&)> callback) {
    // Start a new async read operation
    boost::asio::async_read_until(socket_, responseBuffer_, '\n',
        [this, callback](const boost::system::error_code& error, std::size_t bytesTransferred) {
            if (!error) {
                std::string receivedData;
                // Move data from the buffer to the string until the delimiter is found
                std::istream is(&responseBuffer_);
                std::getline(is, receivedData);
                
                // Add the delimiter back to the string
                receivedData += '\n';

                // Call the user-provided callback
                callback(receivedData);

                // Continue reading
                this->asyncRead(callback);
            } else if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset) {
                // Handle disconnection
                spdlog::warn("Server connection closed.");
            } else {
                spdlog::error("Asynchronous read error: {}", error.message());
            }
        });
}

/**
 * @brief Asynchronously writes data to the socket.
 * @param data The string data to be sent.
 */
void TcpClient::asyncWrite(const std::string& data) {
    boost::asio::async_write(socket_, boost::asio::buffer(data),
        [this, data](const boost::system::error_code& error, std::size_t bytesTransferred) {
            if (!error) {
                spdlog::debug("Successfully transmitted {} bytes of data.", bytesTransferred);
            } else {
                spdlog::error("Asynchronous write error: {}", error.message());
            }
        });
}