#pragma once
#include <functional>
#include <string>
#include <cstdint>

namespace kohzu::comm {

class ITcpClient {
public:
    using RecvHandler = std::function<void(const std::string&)>;

    virtual ~ITcpClient() = default;

    virtual void connect(const std::string& host, uint16_t port) = 0;
    virtual void disconnect() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isConnected() const noexcept = 0;

    virtual void registerRecvHandler(RecvHandler h) = 0;
    virtual void sendLine(const std::string& line) = 0;

    // set callback invoked when connection is lost
    virtual void setOnDisconnect(std::function<void()> cb) {
        (void)cb;
    }
};

} // namespace kohzu::comm
