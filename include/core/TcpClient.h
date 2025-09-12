#pragma once

#include "ICommunicationClient.h"
#include <boost/asio.hpp>
#include <string>
#include <functional>
#include <memory>
#include <mutex>

using namespace boost::asio;
using ip::tcp;

/**
 * @class TcpClient
 * @brief TCP/IP 통신을 위한 클라이언트 클래스.
 * ICommunicationClient 인터페이스를 구현하며, Boost.Asio를 사용해 비동기 통신을 처리합니다.
 */
class TcpClient : public ICommunicationClient {
public:
    TcpClient(boost::asio::io_context& io_context, const std::string& host, const std::string& port);
    ~TcpClient();

    void connect(const std::string& host, const std::string& port) override;
    void asyncWrite(const std::string& data) override;
    void asyncRead(std::function<void(const std::string&)> callback) override;

private:
    void doConnect(const tcp::resolver::results_type& endpoints);
    void doRead();
    void doWrite(const std::string& data);

    boost::asio::io_context& io_context_;
    tcp::socket socket_;
    tcp::resolver resolver_;
    std::string host_;
    std::string port_;
    std::vector<char> read_buffer_;
    std::string read_data_;
    std::function<void(const std::string&)> read_callback_;
    std::mutex write_mutex_;
};
