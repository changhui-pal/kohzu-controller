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
#include "../config/Config.hpp"

namespace kohzu::controller {

class MotorController {
public:
    using Response = kohzu::protocol::Response;
    using AsyncCallback = std::function<void(const Response&, std::exception_ptr)>;
    using OperationCallback = std::function<void(int)>;

    MotorController(std::shared_ptr<kohzu::comm::ITcpClient> tcp,
                    std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher);
    ~MotorController();

    void start();
    void stop();

    std::future<Response> sendAsync(const std::string& cmd, const std::vector<std::string>& params);
    Response sendSync(const std::string& cmd, const std::vector<std::string>& params, std::chrono::milliseconds timeout);
    void sendAsync(const std::string& cmd, const std::vector<std::string>& params, AsyncCallback cb);

    void registerSpontaneousHandler(kohzu::protocol::Dispatcher::SpontaneousHandler h);
    void registerOperationCallbacks(OperationCallback onStart, OperationCallback onFinish);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kohzu::controller
