#pragma once
#include "ITcpClient.hpp"
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <functional>

namespace kohzu::comm {

class AsioTcpClient : public ITcpClient {
public:
    AsioTcpClient();
    ~AsioTcpClient() override;

    void start() override;
    void stop() override;

    void connect(const std::string& host, uint16_t port) override;
    void disconnect() override;

    // Non-blocking connect with completion callback (optional)
    void asyncConnect(const std::string& host,
                      uint16_t port,
                      std::function<void(bool, std::exception_ptr)> callback) override;

    // Register on-disconnect callback
    void setOnDisconnect(std::function<void()> cb) override;

    void sendLine(const std::string& line) override;
    void setRecvHandler(RecvHandler h) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kohzu::comm
