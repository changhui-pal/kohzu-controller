#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include "ICommunicationClient.h"
#include "spdlog/spdlog.h"
#include <string>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

class TcpClient : public ICommunicationClient {
public:
    TcpClient(boost::asio::io_context& io_context, const std::string& host, const std::string& port);
    ~TcpClient();

    void connect(const std::string& host, const std::string& port) override;
    void disconnect() override;
    void write(const std::string& message) override;
    void setReadCallback(MessageCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;

private:
    void do_connect(const boost::asio::ip::tcp::resolver::results_type& endpoints);
    void do_read();
    void do_write();
    void start_keep_alive_timer();
    void check_keep_alive(const boost::system::error_code& error);
    void handle_error(const boost::system::error_code& error, const std::string& description);

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::streambuf read_buffer_;
    std::string host_;
    std::string port_;

    MessageCallback read_callback_;
    ErrorCallback error_callback_;

    std::deque<std::string> write_queue_;
    std::mutex write_queue_mutex_;

    boost::asio::steady_timer keep_alive_timer_;
    static constexpr int RECONNECT_ATTEMPTS = 5;
    static constexpr std::chrono::seconds RECONNECT_INTERVAL{3};
    static constexpr std::chrono::seconds KEEP_ALIVE_INTERVAL{5};

    std::atomic<int> reconnect_count_ = 0;
};

#endif // TCP_CLIENT_H
