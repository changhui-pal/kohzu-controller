#include "core/TcpClient.h"
#include "spdlog/spdlog.h"
#include "protocol/exceptions/ConnectionException.h"
#include <iostream>
#include <boost/asio.hpp>

/**
 * @brief Constructor for TcpClient.
 * @param io_context The Boost.Asio I/O context.
 * @param host The hostname or IP address to connect to.
 * @param port The port number to connect to.
 */
TcpClient::TcpClient(boost::asio::io_context& io_context, const std::string& host, const std::string& port)
    : socket_(io_context),
      resolver_(io_context) {
    spdlog::info("TcpClient 객체 생성: {}:{}", host, port);
}

/**
 * @brief Connects to the specified host and port.
 * @param host The hostname or IP address.
 * @param port The port number.
 */
void TcpClient::connect(const std::string& host, const std::string& port) {
    try {
        boost::asio::connect(socket_, resolver_.resolve(host, port));
        spdlog::info("서버에 성공적으로 연결되었습니다: {}:{}", host, port);
    } catch (const boost::system::system_error& e) {
        throw ConnectionException("연결 실패: " + std::string(e.what()));
    }
}

/**
 * @brief Asynchronously reads data from the socket.
 * @param callback The callback function to be called when data is received.
 */
void TcpClient::asyncRead(std::function<void(const std::string&)> callback) {
    // Start a new async read operation
    boost::asio::async_read_until(socket_, response_buffer_, '\n',
        [this, callback](const boost::system::error_code& error, std::size_t bytes_transferred) {
            if (!error) {
                std::string received_data;
                // Move data from the buffer to the string until the delimiter is found
                std::istream is(&response_buffer_);
                std::getline(is, received_data);
                
                // Add the delimiter back to the string
                received_data += '\n';

                // Call the user-provided callback
                callback(received_data);

                // Continue reading
                this->asyncRead(callback);
            } else if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset) {
                // Handle disconnection
                spdlog::warn("서버 연결이 종료되었습니다.");
            } else {
                spdlog::error("비동기 읽기 오류: {}", error.message());
            }
        });
}

/**
 * @brief Asynchronously writes data to the socket.
 * @param data The string data to be sent.
 */
void TcpClient::asyncWrite(const std::string& data) {
    boost::asio::async_write(socket_, boost::asio::buffer(data),
        [this, data](const boost::system::error_code& error, std::size_t bytes_transferred) {
            if (!error) {
                spdlog::debug("데이터 {} 바이트 전송 완료.", bytes_transferred);
            } else {
                spdlog::error("비동기 쓰기 오류: {}", error.message());
            }
        });
}
