// src/comm/AsioTcpClient.cpp
#include "comm/AsioTcpClient.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <system_error>

namespace kohzu::comm {

struct AsioTcpClient::Impl {
    Impl()
        : resolver_(ioContext_), socket_(ioContext_), connected_(false) {}

    boost::asio::io_context ioContext_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ip::tcp::socket socket_;
    std::thread ioThread_;
    std::mutex cbMtx_;

    std::atomic<bool> connected_;
    ITcpClient::RecvHandler recvHandler_;
    std::function<void()> onDisconnect_;
};

AsioTcpClient::AsioTcpClient()
    : impl_(std::make_unique<Impl>()) {}

AsioTcpClient::~AsioTcpClient() {
    try { stop(); } catch (...) {}
}

void AsioTcpClient::start() {
    // start io context thread if not already
    if (!impl_->ioThread_.joinable()) {
        impl_->ioThread_ = std::thread([this]() {
            try {
                impl_->ioContext_.run();
            } catch (const std::exception& e) {
                std::cerr << "[AsioTcpClient] io_context.run threw: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[AsioTcpClient] io_context.run threw unknown\n";
            }
        });
    }
}

void AsioTcpClient::stop() {
    try {
        impl_->ioContext_.stop();
    } catch (...) {}
    if (impl_->ioThread_.joinable()) {
        impl_->ioThread_.join();
    }
    try {
        if (impl_->socket_.is_open()) impl_->socket_.close();
    } catch (...) {}
    impl_->connected_.store(false);
}

void AsioTcpClient::connect(const std::string& host, uint16_t port) {
    boost::asio::ip::tcp::resolver::results_type endpoints = impl_->resolver_.resolve(host, std::to_string(port));
    boost::asio::connect(impl_->socket_, endpoints);
    impl_->connected_.store(true);

    // start read loop
    auto self = this;
    boost::asio::post(impl_->ioContext_, [this, self]() {
        auto buf = std::make_shared<boost::asio::streambuf>();
        auto readHandler = [this, buf](const boost::system::error_code& ec, std::size_t bytes) {
            if (ec) {
                // disconnect handling
                impl_->connected_.store(false);
                std::function<void()> cb;
                {
                    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                    cb = impl_->onDisconnect_;
                }
                if (cb) {
                    try {
                        impl_->ioContext_.post([cb]() { try { cb(); } catch (...) {} });
                    } catch (...) {
                        try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {}
                    }
                }
                return;
            }
            std::istream is(buf.get());
            std::string line;
            std::getline(is, line);
            ITcpClient::RecvHandler handler;
            {
                std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                handler = impl_->recvHandler_;
            }
            if (handler) {
                try { handler(line); } catch (...) { /* swallow */ }
            }
            // schedule next read
            boost::asio::async_read_until(impl_->socket_, *buf, '\n',
                [this, buf](const boost::system::error_code& ec2, std::size_t) {
                    // reuse same handler (simplified)
                });
        };

        // initial async read
        try {
            boost::asio::async_read_until(impl_->socket_, *buf, '\n',
                [this, buf, readHandler](const boost::system::error_code& ec2, std::size_t bytes) {
                    readHandler(ec2, bytes);
                });
        } catch (...) {}
    });
}

void AsioTcpClient::asyncConnect(const std::string& host, uint16_t port,
                                 std::function<void(bool, std::exception_ptr)> callback) {
    // Run blocking connect on a detached thread so caller is non-blocking.
    std::thread([this, host, port, cb = std::move(callback)]() mutable {
        try {
            this->connect(host, port);
            if (cb) cb(true, nullptr);
        } catch (...) {
            if (cb) cb(false, std::current_exception());
        }
    }).detach();
}

void AsioTcpClient::setOnDisconnect(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
    impl_->onDisconnect_ = std::move(cb);
}

void AsioTcpClient::disconnect() {
    try { impl_->socket_.close(); } catch (...) {}
    impl_->connected_.store(false);
    std::function<void()> cb;
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx_);
        cb = impl_->onDisconnect_;
    }
    if (cb) {
        try {
            impl_->ioContext_.post([cb]() { try { cb(); } catch (...) {} });
        } catch (...) {
            try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {}
        }
    }
}

void AsioTcpClient::sendLine(const std::string& line) {
    if (!impl_->connected_.load()) throw std::runtime_error("Not connected");
    boost::asio::post(impl_->ioContext_, [this, line]() {
        boost::system::error_code ec;
        boost::asio::write(impl_->socket_, boost::asio::buffer(line + "\r\n"), ec);
        if (ec) {
            std::cerr << "[AsioTcpClient] async_write error: " << ec.message() << std::endl;
            impl_->connected_.store(false);
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                cb = impl_->onDisconnect_;
            }
            if (cb) {
                try {
                    impl_->ioContext_.post([cb]() { try { cb(); } catch (...) {} });
                } catch (...) {
                    try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {}
                }
            }
        }
    });
}

void AsioTcpClient::setRecvHandler(RecvHandler h) {
    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
    impl_->recvHandler_ = std::move(h);
}

} // namespace kohzu::comm
