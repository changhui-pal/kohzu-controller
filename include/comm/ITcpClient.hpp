#pragma once

#include <functional>
#include <string>
#include <cstdint>

namespace kohzu::comm {

class ITcpClient {
public:
    using RecvHandler = std::function<void(const std::string& line)>;

    virtual ~ITcpClient() = default;

    virtual void connect(const std::string& host, uint16_t port) = 0;
    virtual void disconnect() = 0;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual bool isConnected() const noexcept = 0;

    virtual void registerRecvHandler(RecvHandler handler) = 0;
    virtual void sendLine(const std::string& line) = 0;

    // called when socket disconnected/fatal IO error
    virtual void setOnDisconnect(std::function<void()> cb) {
        (void)cb;
    }
};

} // namespace kohzu::comm
