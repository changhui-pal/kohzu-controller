#include "comm/AsioTcpClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read_until.hpp>

#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>

namespace kohzu::comm {

struct AsioTcpClient::Impl {
    Impl()
        : ioContext_(),
          workGuard_(boost::asio::make_work_guard(ioContext_)),
          socket_(ioContext_),
          writeStrand_(boost::asio::make_strand(ioContext_)),
          readBuffer_(std::make_unique<boost::asio::streambuf>()),
          thread_(),
          connected_{false} {}

    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::io_context::executor_type> writeStrand_;
    std::unique_ptr<boost::asio::streambuf> readBuffer_;

    std::thread thread_;
    std::mutex cbMtx_;
    std::atomic<bool> connected_;
    ITcpClient::RecvHandler recvHandler_;
    std::function<void()> onDisconnect_;

    void startIoThread() {
        std::lock_guard<std::mutex> lk(cbMtx_);
        if (thread_.joinable()) return;
        thread_ = std::thread([this]() {
            try {
                ioContext_.run();
            } catch (const std::exception& ex) {
                std::cerr << "[AsioTcpClient] io_context.run() threw: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[AsioTcpClient] io_context.run() unknown exception\n";
            }
        });
    }

    void stopIoThread() {
        {
            std::lock_guard<std::mutex> lk(cbMtx_);
            workGuard_.reset();
        }
        ioContext_.stop();
        if (thread_.joinable()) {
            try {
                thread_.join();
            } catch (...) {}
        }
    }

    void asyncReadLoop() {
        if (!socket_.is_open()) return;
        auto bufferPtr = readBuffer_.get();
        boost::asio::async_read_until(socket_, *bufferPtr, "\r\n",
            [this, bufferPtr](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (ec) {
                    {
                        std::lock_guard<std::mutex> lk(cbMtx_);
                        connected_.store(false);
                    }
                    std::cerr << "[AsioTcpClient] read error: " << ec.message() << std::endl;
                    std::function<void()> cb;
                    {
                        std::lock_guard<std::mutex> lk(cbMtx_);
                        cb = onDisconnect_;
                    }
                    if (cb) {
                        try {
                            ioContext_.post([cb]() { try { cb(); } catch (...) {} });
                        } catch (...) {
                            try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {}
                        }
                    }
                    return;
                }

                // extract line
                std::string line;
                try {
                    std::istream is(bufferPtr);
                    std::getline(is, line);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                } catch (...) {
                    bufferPtr->consume(bytes_transferred);
                    asyncReadLoop();
                    return;
                }

                bufferPtr->consume(bytes_transferred);
                ITcpClient::RecvHandler handler;
                {
                    std::lock_guard<std::mutex> lk(cbMtx_);
                    handler = recvHandler_;
                }
                if (handler) {
                    try { handler(line); } catch (...) { /* swallow */ }
                }
                // schedule next
                asyncReadLoop();
            });
    }
};

AsioTcpClient::AsioTcpClient()
    : impl_(std::make_unique<Impl>()) {}

AsioTcpClient::~AsioTcpClient() {
    try { stop(); } catch (...) {}
}

void AsioTcpClient::start() {
    impl_->startIoThread();
    if (impl_->connected_.load()) {
        impl_->ioContext_.post([this]() { impl_->asyncReadLoop(); });
    }
}

void AsioTcpClient::stop() {
    impl_->stopIoThread();
}

void AsioTcpClient::connect(const std::string& host, uint16_t port) {
    try {
        boost::asio::ip::tcp::resolver resolver(impl_->ioContext_);
        auto eps = resolver.resolve(host, std::to_string(port));
        boost::asio::connect(impl_->socket_, eps);
        impl_->connected_.store(true);
        impl_->ioContext_.post([this]() { impl_->asyncReadLoop(); });
    } catch (const std::exception& ex) {
        std::ostringstream ss;
        ss << "[AsioTcpClient] connect failed: " << ex.what();
        throw std::runtime_error(ss.str());
    } catch (...) {
        throw std::runtime_error("[AsioTcpClient] connect unknown error");
    }
}

void AsioTcpClient::disconnect() {
    try {
        boost::system::error_code ec;
        impl_->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        impl_->socket_.close(ec);
    } catch (...) {}
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

bool AsioTcpClient::isConnected() const noexcept {
    return impl_->connected_.load();
}

void AsioTcpClient::registerRecvHandler(RecvHandler handler) {
    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
    impl_->recvHandler_ = std::move(handler);
}

void AsioTcpClient::sendLine(const std::string& line) {
    if (!impl_->connected_.load()) throw std::runtime_error("AsioTcpClient::sendLine: not connected");

    std::string out = line;
    if (out.size() < 2 || (out[out.size() - 1] != '\n' || (out[out.size()-2] != '\r'))) {
        // ensure CRLF
        if (!out.empty() && out.back() == '\n') {
            out.insert(out.end()-1, '\r');
        } else {
            out += "\r\n";
        }
    }

    boost::asio::post(impl_->writeStrand_, [this, out = std::move(out)]() {
        boost::system::error_code ec;
        try {
            boost::asio::write(impl_->socket_, boost::asio::buffer(out), ec);
            if (ec) {
                std::cerr << "[AsioTcpClient] write error: " << ec.message() << std::endl;
                {
                    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                    impl_->connected_.store(false);
                }
                std::function<void()> cb;
                {
                    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                    cb = impl_->onDisconnect_;
                }
                if (cb) {
                    try { impl_->ioContext_.post([cb]() { try { cb(); } catch (...) {} }); }
                    catch (...) { try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {} }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[AsioTcpClient] write exception: " << e.what() << std::endl;
            {
                std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                impl_->connected_.store(false);
            }
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                cb = impl_->onDisconnect_;
            }
            if (cb) {
                try { impl_->ioContext_.post([cb]() { try { cb(); } catch (...) {} }); }
                catch (...) { try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {} }
            }
        } catch (...) {
            std::cerr << "[AsioTcpClient] write unknown exception\n";
            {
                std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                impl_->connected_.store(false);
            }
            std::function<void()> cb;
            {
                std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                cb = impl_->onDisconnect_;
            }
            if (cb) {
                try { impl_->ioContext_.post([cb]() { try { cb(); } catch (...) {} }); }
                catch (...) { try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {} }
            }
        }
    });
}

void AsioTcpClient::setOnDisconnect(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
    impl_->onDisconnect_ = std::move(cb);
}

} // namespace kohzu::comm
