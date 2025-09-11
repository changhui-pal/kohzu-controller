#pragma once

#include "ITcpClient.hpp"

#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
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

    bool isConnected() const noexcept override;

    void registerRecvHandler(RecvHandler handler) override;
    void sendLine(const std::string& line) override;

    // notify upper layers on disconnect
    void setOnDisconnect(std::function<void()> cb) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kohzu::comm
