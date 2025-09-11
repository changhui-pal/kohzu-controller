#pragma once
#include <string>
#include <functional>
#include <cstdint>
#include <exception>

namespace kohzu::comm {

class ITcpClient {
public:
    using RecvHandler = std::function<void(const std::string&)>;

    virtual ~ITcpClient() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void connect(const std::string& host, uint16_t port) = 0;
    virtual void disconnect() = 0;

    virtual void sendLine(const std::string& line) = 0;
    virtual void setRecvHandler(RecvHandler h) = 0;

    /**
     * Asynchronous connect helper.
     * Default implementation calls the blocking connect() and invokes the callback.
     * Implementations may override to provide non-blocking connect with timeout/cancellation.
     *
     * callback(bool success, std::exception_ptr ep)
     */
    virtual void asyncConnect(const std::string& host,
                              uint16_t port,
                              std::function<void(bool, std::exception_ptr)> callback) {
        try {
            connect(host, port);
            if (callback) callback(true, nullptr);
        } catch (...) {
            if (callback) callback(false, std::current_exception());
        }
    }

    /**
     * Register a callback invoked when the connection is lost / disconnected.
     * The callback may be invoked from an internal thread; callers should marshal to their
     * desired thread if necessary.
     */
    virtual void setOnDisconnect(std::function<void()> cb) {
        (void)cb;
    }
};

} // namespace kohzu::comm
