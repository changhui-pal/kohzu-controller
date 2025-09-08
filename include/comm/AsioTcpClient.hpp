#pragma once

#include "ITcpClient.hpp"
#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <mutex>

namespace kohzu::comm {

class AsioTcpClient : public ITcpClient, public std::enable_shared_from_this<AsioTcpClient> {
public:
    AsioTcpClient();
    ~AsioTcpClient() override;

    // ITcpClient
    void connect(const std::string& host, uint16_t port) override;
    void close() override;
    void registerRecvHandler(RecvHandler cb) override;
    boost::asio::ip::tcp::socket& socket() override;

    bool isRunning() const;

private:
    void startRead();
    void handleRead(const boost::system::error_code& ec, std::size_t bytesTransferred);

    // io & socket
    boost::asio::io_context ioContext_;
    boost::asio::ip::tcp::socket socket_;
    std::unique_ptr<boost::asio::streambuf> readBuffer_;

    // receive callback and protection
    RecvHandler recvCallback_;
    mutable std::mutex recvCallbackMutex_;

    // background thread running io_context
    std::thread ioThread_;

    bool running_;
};

} // namespace kohzu::comm

