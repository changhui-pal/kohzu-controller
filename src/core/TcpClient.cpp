#include "core/TcpClient.h"
#include "protocol/exceptions/ConnectionException.h"
#include "spdlog/spdlog.h"
#include <iostream>

TcpClient::TcpClient(boost::asio::io_context& io_context, const std::string& host, const std::string& port)
    : io_context_(io_context), socket_(io_context), resolver_(io_context), host_(host), port_(port), read_buffer_(1024) {
    spdlog::info("TcpClient 객체 생성: {}:{}", host, port);
}

TcpClient::~TcpClient() {
    boost::system::error_code ec;
    socket_.close(ec);
    if (ec) {
        spdlog::error("소켓 닫기 오류: {}", ec.message());
    }
}

void TcpClient::connect(const std::string& host, const std::string& port) {
    try {
        tcp::resolver::results_type endpoints = resolver_.resolve(host, port);
        boost::asio::connect(socket_, endpoints);
        spdlog::info("서버에 성공적으로 연결되었습니다: {}:{}", host, port);
    } catch (const boost::system::system_error& e) {
        throw ConnectionException("연결 실패: " + std::string(e.what()));
    }
}

void TcpClient::asyncWrite(const std::string& data) {
    std::lock_guard<std::mutex> lock(write_mutex_);
    boost::asio::async_write(socket_, boost::asio::buffer(data),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
        if (error) {
            spdlog::error("쓰기 오류: {}", error.message());
        } else {
            spdlog::debug("데이터 {} 바이트 전송 완료.", bytes_transferred);
        }
    });
}

void TcpClient::asyncRead(std::function<void(const std::string&)> callback) {
    read_callback_ = callback;
    doRead();
}

void TcpClient::doRead() {
    boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(read_data_), '\n',
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
        if (!error) {
            read_data_.resize(read_data_.size() - 2); // Remove \r\n
            if (read_callback_) {
                read_callback_(read_data_);
            }
            read_data_.clear();
            doRead(); // 다음 읽기 작업 시작
        } else if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset) {
            spdlog::error("연결 종료: {}", error.message());
            // TODO: 재연결 로직 추가
        } else {
            spdlog::error("읽기 오류: {}", error.message());
        }
    });
}
