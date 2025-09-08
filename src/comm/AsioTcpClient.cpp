#include "comm/AsioTcpClient.hpp"
#include <iostream>
#include <system_error>

using boost::asio::ip::tcp;
namespace kc = kohzu::comm;

kc::AsioTcpClient::AsioTcpClient()
: socket_(ioContext_),
  readBuffer_(std::make_unique<boost::asio::streambuf>()),
  recvCallback_(nullptr),
  running_(false)
{
}

kc::AsioTcpClient::~AsioTcpClient() {
    try {
        close();
    } catch (...) {
        // destructor must not throw
    }
}

void kc::AsioTcpClient::connect(const std::string& host, uint16_t port) {
    // Resolve and connect (blocking)
    tcp::resolver resolver(ioContext_);
    boost::asio::ip::basic_resolver_results<tcp> endpoints = resolver.resolve(host, std::to_string(port));
    boost::asio::connect(socket_, endpoints);

    // Disable Nagle for lower latency on small messages
    boost::asio::ip::tcp::no_delay noDelayOption(true);
    boost::system::error_code ecOpt;
    socket_.set_option(noDelayOption, ecOpt);
    if (ecOpt) {
        std::cerr << "Warning: failed to set TCP_NODELAY: " << ecOpt.message() << std::endl;
    }

    // mark running and start read loop before running io_context
    running_ = true;
    startRead();

    // run the io_context on background thread
    ioThread_ = std::thread([this]() {
        try {
            ioContext_.run();
        } catch (const std::exception& e) {
            std::cerr << "AsioTcpClient io_context.run exception: " << e.what() << std::endl;
        }
    });
}

void kc::AsioTcpClient::close() {
    if (!running_) {
        return;
    }

    running_ = false;

    boost::system::error_code ec;
    // cancel pending operations and close socket
    socket_.cancel(ec);
    if (ec) {
        std::cerr << "AsioTcpClient socket.cancel error: " << ec.message() << std::endl;
    }

    socket_.close(ec);
    if (ec) {
        std::cerr << "AsioTcpClient socket.close error: " << ec.message() << std::endl;
    }

    // stop io_context and join thread
    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
}

void kc::AsioTcpClient::registerRecvHandler(RecvHandler cb) {
    std::lock_guard<std::mutex> lock(recvCallbackMutex_);
    recvCallback_ = std::move(cb);
}

boost::asio::ip::tcp::socket& kc::AsioTcpClient::socket() {
    return socket_;
}

bool kc::AsioTcpClient::isRunning() const {
    return running_;
}

void kc::AsioTcpClient::startRead() {
    // Keep object alive via shared_ptr during async operation
    std::shared_ptr<AsioTcpClient> self = shared_from_this();
    boost::asio::async_read_until(socket_, *readBuffer_, "\r\n",
        [this, self](const boost::system::error_code& ec, std::size_t bytesTransferred) {
            handleRead(ec, bytesTransferred);
        });
}

void kc::AsioTcpClient::handleRead(const boost::system::error_code& ec, std::size_t bytesTransferred) {
    if (ec) {
        if (ec == boost::asio::error::operation_aborted) {
            // expected during close()
            return;
        }
        std::cerr << "AsioTcpClient read error: " << ec.message() << std::endl;
        // Optionally notify user via callback that connection is broken (not implemented here)
        return;
    }

    // Extract the line (CRLF stripped)
    std::istream is(readBuffer_.get());
    std::string line;
    std::getline(is, line);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    // Call user callback (thread from io_context)
    {
        std::lock_guard<std::mutex> lock(recvCallbackMutex_);
        if (recvCallback_) {
            try {
                recvCallback_(line);
            } catch (const std::exception& e) {
                std::cerr << "recvCallback threw exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "recvCallback threw unknown exception" << std::endl;
            }
        }
    }

    // consume the used bytes and continue reading
    readBuffer_->consume(bytesTransferred);

    if (running_) {
        startRead();
    }
}

