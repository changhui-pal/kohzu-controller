#pragma once

// include/controller/MotorController.hpp

#include "comm/ITcpClient.hpp"
#include "comm/Writer.hpp"
#include "protocol/Dispatcher.hpp"
#include "protocol/CommandBuilder.hpp"
#include "protocol/Parser.hpp"
#include "config/Config.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>

namespace kohzu::controller {

class MotorController {
public:
    explicit MotorController(std::shared_ptr<kohzu::comm::ITcpClient> comm);

    ~MotorController();

    void connect(const std::string& host, uint16_t port);

    void stop();

    // existing API: returns future (caller may process it)
    kohzu::protocol::Response sendSync(const std::string& cmd, const std::vector<std::string>& params,
                                       std::chrono::milliseconds timeout = kohzu::config::DEFAULT_RESPONSE_TIMEOUT_MS);

    std::future<kohzu::protocol::Response> sendAsync(const std::string& cmd, const std::vector<std::string>& params);

    // NEW: callback-style sendAsync.
    // callback signature: void(const kohzu::protocol::Response& resp, std::exception_ptr ep)
    // - if ep == nullptr -> resp contains valid response
    // - if ep != nullptr -> resp is unspecified; ep describes the error (e.g., enqueue failed, timeout, dispatcher exception)
    // NOTE: callback is invoked from the internal worker thread. If you need GUI-safe handling,
    //       forward the result to the UI thread (e.g., via Qt signals or QMetaObject::invokeMethod).
    using AsyncCallback = std::function<void(const kohzu::protocol::Response&, std::exception_ptr)>;
    void sendAsync(const std::string& cmd, const std::vector<std::string>& params, AsyncCallback callback);

    static std::string makeMatchKey(const std::string& cmd, const std::vector<std::string>& params);

    void registerSpontaneousHandler(kohzu::protocol::SpontaneousHandler handler);

private:
    void onLineReceived(const std::string& line);

    // worker loop for handling callback-style futures
    void callbackWorkerLoop();

    // data members
    std::shared_ptr<kohzu::comm::ITcpClient> comm_;
    std::unique_ptr<kohzu::comm::Writer> writer_;
    kohzu::protocol::Dispatcher dispatcher_;

    // callback worker internals
    struct CallbackTask {
        std::future<kohzu::protocol::Response> fut;
        AsyncCallback cb;
    };

    std::thread callbackWorkerThread_;
    std::deque<CallbackTask> callbackQueue_;
    std::mutex callbackQueueMutex_;
    std::condition_variable callbackQueueCv_;
    std::atomic<bool> callbackWorkerStopRequested_{false};

    // small helper to push a task (thread-safe)
    void pushCallbackTask(CallbackTask&& task);
};

} // namespace kohzu::controller

