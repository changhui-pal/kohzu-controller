#include "comm/AsioTcpClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read_until.hpp>

#include <iostream>
#include <sstream>
#include <system_error>
#include <utility>
#include <cassert>

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

    ~Impl() { /* cleanup handled by outer */ }

    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::io_context::executor_type> writeStrand_;
    std::unique_ptr<boost::asio::streambuf> readBuffer_;

    std::thread thread_;
    std::mutex cbMtx_; // protects recvHandler_, onDisconnect_, connected_
    std::atomic<bool> connected_;
    ITcpClient::RecvHandler recvHandler_;
    std::function<void()> onDisconnect_;

    // start io_context thread
    void startIo() {
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

    // stop io_context thread
    void stopIo() {
        {
            std::lock_guard<std::mutex> lk(cbMtx_);
            // reset work guard so run() can exit
            workGuard_.reset();
        }
        ioContext_.stop();
        if (thread_.joinable()) {
            try {
                thread_.join();
            } catch (const std::exception& e) {
                std::cerr << "[AsioTcpClient] thread join error: " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[AsioTcpClient] thread join unknown error\n";
            }
        }
    }

    // schedule the next async read if socket open
    void asyncReadLine() {
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

                    // notify onDisconnect if set
                    std::function<void()> cb;
                    {
                        std::lock_guard<std::mutex> lk(cbMtx_);
                        cb = onDisconnect_;
                    }
                    if (cb) {
                        try {
                            // post callback onto io_context for consistent context
                            ioContext_.post([cb]() {
                                try { cb(); } catch (...) { /* swallow */ }
                            });
                        } catch (...) {
                            try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {}
                        }
                    }
                    return;
                }

                // read line from buffer
                std::string line;
                try {
                    std::istream is(bufferPtr);
                    std::getline(is, line);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                } catch (const std::exception& ex) {
                    std::cerr << "[AsioTcpClient] parse readBuffer error: " << ex.what() << std::endl;
                    bufferPtr->consume(bytes_transferred);
                    // attempt to continue reading
                    asyncReadLine();
                    return;
                } catch (...) {
                    std::cerr << "[AsioTcpClient] parse readBuffer unknown error\n";
                    bufferPtr->consume(bytes_transferred);
                    asyncReadLine();
                    return;
                }

                // consume bytes and dispatch to recv handler (if any)
                bufferPtr->consume(bytes_transferred);

                ITcpClient::RecvHandler handler;
                {
                    std::lock_guard<std::mutex> lk(cbMtx_);
                    handler = recvHandler_;
                }

                if (handler) {
                    try {
                        handler(line);
                    } catch (const std::exception& e) {
                        std::cerr << "[AsioTcpClient] recv handler threw: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "[AsioTcpClient] recv handler unknown exception\n";
                    }
                }

                // schedule next read
                asyncReadLine();
            });
    }
};

AsioTcpClient::AsioTcpClient()
    : impl_(std::make_unique<Impl>()) {}

AsioTcpClient::~AsioTcpClient() {
    try {
        stop();
    } catch (...) {}
}

void AsioTcpClient::start() {
    impl_->startIo();
    // if connected, start read loop
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx_);
        if (impl_->connected_.load()) {
            impl_->asyncReadLine();
        }
    }
}

void AsioTcpClient::stop() {
    impl_->stopIo();
}

void AsioTcpClient::connect(const std::string& host, uint16_t port) {
    try {
        boost::asio::ip::tcp::resolver resolver(impl_->ioContext_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::connect(impl_->socket_, endpoints);
        // set TCP_NODELAY etc if desired
        boost::asio::ip::tcp::no_delay option(true);
        boost::system::error_code ec;
        impl_->socket_.set_option(option, ec);
        if (ec) {
            std::cerr << "[AsioTcpClient] set_option no_delay failed: " << ec.message() << std::endl;
        }
        {
            std::lock_guard<std::mutex> lk(impl_->cbMtx_);
            impl_->connected_.store(true);
        }
        // after connected, schedule read loop (must be posted on io context)
        impl_->ioContext_.post([this]() {
            try { impl_->asyncReadLine(); } catch (...) {}
        });
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
        if (ec) {
            std::cerr << "[AsioTcpClient] socket close error: " << ec.message() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "[AsioTcpClient] disconnect error: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[AsioTcpClient] disconnect unknown error\n";
    }

    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx_);
        impl_->connected_.store(false);
    }

    // notify onDisconnect if set
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

    std::string toSend = line;
    // ensure CRLF
    if (toSend.size() < 2 || (toSend[toSend.size()-2] != '\r' || toSend[toSend.size()-1] != '\n')) {
        if (!toSend.empty() && toSend.back() == '\n') {
            toSend.insert(toSend.end()-1, '\r');
        } else {
            toSend += "\r\n";
        }
    }

    // Post write on strand to serialize writes
    boost::asio::post(impl_->writeStrand_, [this, toSend = std::move(toSend)]() {
        boost::system::error_code ec;
        try {
            boost::asio::write(impl_->socket_, boost::asio::buffer(toSend), ec);
            if (ec) {
                std::cerr << "[AsioTcpClient] write error: " << ec.message() << std::endl;
                {
                    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
                    impl_->connected_.store(false);
                }
                // notify onDisconnect if set
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
                try {
                    impl_->ioContext_.post([cb]() { try { cb(); } catch (...) {} });
                } catch (...) {
                    try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {}
                }
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
                try {
                    impl_->ioContext_.post([cb]() { try { cb(); } catch (...) {} });
                } catch (...) {
                    try { std::thread([cb](){ try { cb(); } catch (...) {} }).detach(); } catch (...) {}
                }
            }
        }
    });
}

void AsioTcpClient::setOnDisconnect(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
    impl_->onDisconnect_ = std::move(cb);
}

} // namespace kohzu::comm
