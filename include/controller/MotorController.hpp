#pragma once

#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <future>
#include <chrono>

#include "../comm/ITcpClient.hpp"
#include "../protocol/Dispatcher.hpp"
#include "../protocol/Parser.hpp"

namespace kohzu::controller {

class MotorController {
public:
    using Response = kohzu::protocol::Response;
    using AsyncCallback = std::function<void(const Response&, std::exception_ptr)>;
    using OperationCallback = std::function<void(int)>;

    MotorController(std::shared_ptr<kohzu::comm::ITcpClient> client,
                    std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher);
    ~MotorController();

    void start();
    void stop();

    // send async returns future<Response>
    std::future<Response> sendAsync(const std::string& cmd, const std::vector<std::string>& params);

    // synchronous wrapper that waits up to timeout
    Response sendSync(const std::string& cmd, const std::vector<std::string>& params, std::chrono::milliseconds timeout);

    // convenience async that takes callback
    void sendAsync(const std::string& cmd,
                   const std::vector<std::string>& params,
                   AsyncCallback cb);

    void registerSpontaneousHandler(Dispatcher::SpontaneousHandler h);
    void registerOperationCallbacks(OperationCallback onStart, OperationCallback onFinish);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kohzu::controller
