// src/comm/AsioTcpClient.cpp
#include "comm/AsioTcpClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read_until.hpp>

#include <iostream>
#include <sstream>
#include <system_error>

namespace kohzu::comm {

struct AsioTcpClient::Impl {
    Impl()
        : ioContext_(),
          workGuard_(boost::asio::make_work_guard(ioContext_)),
          socket_(ioContext_),
          writeStrand_(boost::asio::make_strand(ioContext_)),
          thread_(),
          connected_{false} {}

    ~Impl() {
        // cleanup handled by AsioTcpClient destructor
    }

    // Start io_context running in background thread
    void startIo() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (thread_.joinable()) return;
        thread_ = std::thread([this]() {
            try {
                ioContext_.run();
            } catch (const std::exception& ex) {
                std::cerr << "[AsioTcpClient] io_context.run() threw: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[AsioTcpClient] io_context.run() unknown exception\n"; // 수정: catch (...) 로그
            }
        });
    }

    // Stop io_context and join thread
    void stopIo() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!thread_.joinable()) return;
            // release work guard to let run() exit when no pending handlers
            workGuard_.reset();
        }
        ioContext_.stop();
        if (thread_.joinable()) {
            try {
                thread_.join();
            } catch (const std::exception& e) {
                std::cerr << "[AsioTcpClient] thread join error: " << e.what() << "\n"; // 수정: join 예외 처리
            } catch (...) {
                std::cerr << "[AsioTcpClient] thread join unknown error\n";
            }
        }
    }

    // schedule next async read (must be called while socket is open)
    void asyncReadLine() {
        boost::asio::async_read_until(socket_, *readBuffer_, "\r\n",
            [this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                if (ec) {
                    // On read error, mark disconnected and call recv handler with nothing or log
                    {
                        std::lock_guard<std::mutex> lk(cbMtx_);
                        connected_.store(false);
                    }
                    std::cerr << "[AsioTcpClient] read error: " << ec.message() << std::endl;
                    // don't attempt further reads if socket closed
                    return;
                }

                std::string line;
                try {
                    std::istream is(readBuffer_.get());
                    std::getline(is, line); // reads up to '\n', removes it
                    // remove trailing '\r' if present
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                } catch (const std::exception& ex) {
                    std::cerr << "[AsioTcpClient] parse readBuffer error: " << ex.what() << std::endl;
                    // continue reading
                    readBuffer_->consume(bytes_transferred);
                    asyncReadLine();
                    return;
                } catch (...) {
                    std::cerr << "[AsioTcpClient] parse readBuffer unknown error\n"; // 수정: catch (...) 로그
                    readBuffer_->consume(bytes_transferred);
                    asyncReadLine();
                    return;
                }

                // consume bytes_transferred from buffer
                readBuffer_->consume(bytes_transferred);

                // call registered recv handler (if any) on io thread
                RecvHandler handler;
                {
                    std::lock_guard<std::mutex> lk(cbMtx_);
                    handler = recvHandler_;
                }
                if (handler) {
                    try {
                        handler(line);
                    } catch (const std::exception& ex) {
                        std::cerr << "[AsioTcpClient] recv handler threw: " << ex.what() << std::endl;
                    } catch (...) {
                        std::cerr << "[AsioTcpClient] recv handler threw unknown exception" << std::endl;
                    }
                }

                // schedule next read
                asyncReadLine();
            });
    }

    boost::asio::io_context ioContext_;
    // keep io_context alive until explicit stop
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::strand<boost::asio::io_context::executor_type> writeStrand_;
    std::unique_ptr<boost::asio::streambuf> readBuffer_ = std::make_unique<boost::asio::streambuf>();

    std::thread thread_;
    std::mutex mtx_; // protects thread existence and workGuard_
    std::mutex cbMtx_; // protects recvHandler_ and connected_
    std::atomic<bool> connected_;
    RecvHandler recvHandler_;
};

AsioTcpClient::AsioTcpClient()
    : impl_(std::make_unique<Impl>()) {
}

AsioTcpClient::~AsioTcpClient() {
    try {
        stop();
        disconnect();
    } catch (const std::exception& e) {
        std::cerr << "[AsioTcpClient::~] cleanup error: " << e.what() << "\n"; // 수정: dtor 예외 로그 (throw 금지)
    } catch (...) {
        std::cerr << "[AsioTcpClient::~] cleanup unknown error\n";
    }
}

void AsioTcpClient::connect(const std::string& host, uint16_t port) {
    using boost::asio::ip::tcp;
    try {
        tcp::resolver resolver(impl_->ioContext_);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::connect(impl_->socket_, endpoints);
        // set TCP_NODELAY
        boost::system::error_code ec;
        impl_->socket_.set_option(tcp::no_delay(true), ec);
        if (ec) {
            std::cerr << "[AsioTcpClient] set_option no_delay failed: " << ec.message() << std::endl;
        }
        {
            std::lock_guard<std::mutex> lk(impl_->cbMtx_);
            impl_->connected_.store(true);
        }
    } catch (const std::exception& ex) {
        std::ostringstream ss;
        ss << "[AsioTcpClient] connect failed: " << ex.what();
        throw std::runtime_error(ss.str());
    } catch (...) {
        throw std::runtime_error("[AsioTcpClient] connect unknown error"); // 수정: catch (...) 예외 변환
    }
}

void AsioTcpClient::disconnect() {
    // close socket safely
    try {
        boost::system::error_code ec;
        impl_->socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        impl_->socket_.close(ec);
        if (ec) {
            std::cerr << "[AsioTcpClient] socket close error: " << ec.message() << "\n"; // 수정: 에러 로그
        }
    } catch (const std::exception& e) {
        std::cerr << "[AsioTcpClient] disconnect error: " << e.what() << "\n"; // 수정: 예외 로그
    } catch (...) {
        std::cerr << "[AsioTcpClient] disconnect unknown error\n";
    }
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx_);
        impl_->connected_.store(false);
    }
}

void AsioTcpClient::start() {
    // start io thread and begin async read
    impl_->startIo();
    // schedule first async read
    impl_->asyncReadLine();
}

void AsioTcpClient::stop() {
    // stop io and join thread
    impl_->stopIo();
}

bool AsioTcpClient::isConnected() const noexcept {
    return impl_->connected_.load();
}

void AsioTcpClient::registerRecvHandler(RecvHandler handler) {
    std::lock_guard<std::mutex> lk(impl_->cbMtx_);
    impl_->recvHandler_ = std::move(handler);
}

void AsioTcpClient::sendLine(const std::string& line) {
    // sendLine: post an async write on writeStrand_
    if (!impl_->connected_.load()) {
        throw std::runtime_error("AsioTcpClient::sendLine: not connected");
    }

    std::string toSend = line;
    // ensure CRLF
    if (toSend.size() < 2 || (toSend[toSend.size()-2] != '\r' || toSend[toSend.size()-1] != '\n')) {
        // if already ends with \n but no \r, add \r
        if (!toSend.empty() && toSend.back() == '\n') {
            toSend.insert(toSend.end()-1, '\r');
        } else {
            toSend += "\r\n";
        }
    }

    // capture a weak_ptr-like handle via raw pointer to impl; it's safe because impl_ owned by this instance
    boost::asio::post(impl_->writeStrand_, [impl = impl_.get(), buf = std::move(toSend)]() mutable {
        // perform async_write; error handling logged here
        boost::asio::async_write(impl->socket_, boost::asio::buffer(buf),
            [impl](const boost::system::error_code& ec, std::size_t /*bytes_transferred*/) {
                if (ec) {
                    std::cerr << "[AsioTcpClient] async_write error: " << ec.message() << std::endl;
                    // On write error we mark disconnected.
                    {
                        std::lock_guard<std::mutex> lk(impl->cbMtx_);
                        impl->connected_.store(false);
                    }
                }
            });
    });
}

} // namespace kohzu::comm
