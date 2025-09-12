#include "core/TcpClient.h"
#include <iostream>

TcpClient::TcpClient(boost::asio::io_context& io_context, const std::string& host, const std::string& port)
    : io_context_(io_context),
      socket_(io_context),
      resolver_(io_context),
      host_(host),
      port_(port),
      keep_alive_timer_(io_context) {
    spdlog::info("TcpClient 객체가 생성되었습니다.");
}

TcpClient::~TcpClient() {
    disconnect();
    spdlog::info("TcpClient 객체가 소멸되었습니다.");
}

void TcpClient::connect(const std::string& host, const std::string& port) {
    host_ = host;
    port_ = port;
    reconnect_count_ = 0;
    spdlog::info("서버에 연결을 시도합니다: {}:{}", host_, port_);
    boost::asio::post(io_context_, [this]() {
        resolver_.async_resolve(host_, port_, [this](const boost::system::error_code& error,
                                                 const boost::asio::ip::tcp::resolver::results_type& endpoints) {
            if (!error) {
                do_connect(endpoints);
            } else {
                handle_error(error, "도메인 이름 해결 실패");
            }
        });
    });
}

void TcpClient::do_connect(const boost::asio::ip::tcp::resolver::results_type& endpoints) {
    boost::asio::async_connect(socket_, endpoints, [this](const boost::system::error_code& error,
                                                         const boost::asio::ip::tcp::endpoint& endpoint) {
        if (!error) {
            spdlog::info("서버에 성공적으로 연결되었습니다: {}", endpoint.address().to_string());
            reconnect_count_ = 0;
            do_read();
            start_keep_alive_timer();
        } else {
            handle_error(error, "연결 실패");
            if (reconnect_count_ < RECONNECT_ATTEMPTS) {
                reconnect_count_++;
                spdlog::warn("재연결을 시도합니다 ({} / {})...", reconnect_count_.load(), RECONNECT_ATTEMPTS);
                std::this_thread::sleep_for(RECONNECT_INTERVAL);
                connect(host_, port_);
            } else {
                spdlog::error("최대 재연결 횟수 초과. 연결 시도를 중단합니다.");
                if (error_callback_) {
                    error_callback_("최대 재연결 횟수 초과");
                }
            }
        }
    });
}

void TcpClient::disconnect() {
    spdlog::info("연결을 해제합니다.");
    keep_alive_timer_.cancel();
    boost::system::error_code ec;
    socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket_.close(ec);
}

void TcpClient::write(const std::string& message) {
    boost::asio::post(io_context_, [this, message]() {
        std::lock_guard<std::mutex> lock(write_queue_mutex_);
        bool write_in_progress = !write_queue_.empty();
        write_queue_.push_back(message);
        if (!write_in_progress) {
            do_write();
        }
    });
}

void TcpClient::do_write() {
    boost::asio::async_write(socket_, boost::asio::buffer(write_queue_.front()),
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            if (!error) {
                spdlog::info("메시지 전송 성공: {}", write_queue_.front());
                std::lock_guard<std::mutex> lock(write_queue_mutex_);
                write_queue_.pop_front();
                if (!write_queue_.empty()) {
                    do_write();
                }
            } else {
                handle_error(error, "쓰기 작업 실패");
                std::lock_guard<std::mutex> lock(write_queue_mutex_);
                write_queue_.clear();
            }
        });
}

void TcpClient::do_read() {
    boost::asio::async_read_until(socket_, read_buffer_, "\r\n",
        [this](const boost::system::error_code& error, size_t bytes_transferred) {
            if (!error) {
                std::istream is(&read_buffer_);
                std::string line;
                std::getline(is, line);
                spdlog::info("메시지 수신 성공: {}", line);
                if (read_callback_) {
                    read_callback_(line);
                }
                do_read(); // 다음 읽기 작업 시작
            } else {
                handle_error(error, "읽기 작업 실패");
            }
        });
}

void TcpClient::setReadCallback(MessageCallback callback) {
    read_callback_ = callback;
}

void TcpClient::setErrorCallback(ErrorCallback callback) {
    error_callback_ = callback;
}

void TcpClient::start_keep_alive_timer() {
    keep_alive_timer_.expires_from_now(KEEP_ALIVE_INTERVAL);
    keep_alive_timer_.async_wait([this](const boost::system::error_code& error) {
        check_keep_alive(error);
    });
}

void TcpClient::check_keep_alive(const boost::system::error_code& error) {
    if (error == boost::asio::error::operation_aborted) {
        return; // 타이머가 취소되었을 경우
    }
    
    if (!error) {
        // 컨트롤러에 상태 확인 명령어(예: STS)를 보냅니다.
        // STS 명령어는 실제 컨트롤러 매뉴얼에 따라 정의해야 합니다.
        spdlog::debug("Keep-Alive 타이머: 상태 확인 명령 전송...");
        write("STS\r\n");
        
        // 다음 타이머를 시작합니다.
        start_keep_alive_timer();
    }
}

void TcpClient::handle_error(const boost::system::error_code& error, const std::string& description) {
    if (error == boost::asio::error::eof || error == boost::asio::error::connection_reset) {
        spdlog::error("연결이 끊어졌습니다: {}", description);
        if (error_callback_) {
            error_callback_("연결이 끊어졌습니다.");
        }
    } else if (error != boost::asio::error::operation_aborted) {
        spdlog::error("Boost.Asio 오류: {}. 설명: {}", error.message(), description);
        if (error_callback_) {
            error_callback_("통신 오류 발생");
        }
    }
}
