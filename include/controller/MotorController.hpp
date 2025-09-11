#pragma once
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <future>
#include "protocol/Response.hpp" // assume this exists
#include "comm/ITcpClient.hpp"
#include "protocol/Dispatcher.hpp"
#include "comm/Writer.hpp"

namespace kohzu::controller {

class MotorController {
public:
    using AsyncCallback = std::function<void(const kohzu::protocol::Response&, std::exception_ptr)>;

    MotorController(std::shared_ptr<kohzu::comm::ITcpClient> client,
                    std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher);
    ~MotorController();

    void start();
    void stop();

    // sync/async sends
    std::future<kohzu::protocol::Response> sendAsync(const std::string& cmd, const std::vector<std::string>& params);
    kohzu::protocol::Response sendSync(const std::string& cmd, const std::vector<std::string>& params, std::chrono::milliseconds timeout);

    void sendAsync(const std::string& cmd,
                   const std::vector<std::string>& params,
                   AsyncCallback cb);

    /**
     * onOperationFinish(axis) callback (optional). Called when an operation for an axis finishes.
     * This may be invoked from a worker thread; caller should synchronize if needed.
     */
    std::function<void(int)> onOperationFinish;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kohzu::controller
