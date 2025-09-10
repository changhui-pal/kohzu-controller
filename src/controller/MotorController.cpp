// src/controller/MotorController.cpp
#include "MotorController.hpp"
#include "../protocol/CommandBuilder.hpp"
#include "../protocol/Parser.hpp"
#include "../comm/Writer.hpp"

#include <iostream>
#include <thread>
#include <queue>
#include <optional>
#include <set>
#include <utility>

namespace kohzu::controller {

struct MotorController::Impl {
    Impl(std::shared_ptr<kohzu::comm::ITcpClient> tcp,
         std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher)
        : tcpClient(std::move(tcp)),
          dispatcher(std::move(dispatcher)),
          writer(nullptr),
          cbWorkerRunning(false),
          stopRequested(false) {}

    std::shared_ptr<kohzu::comm::ITcpClient> tcpClient;
    std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher;
    std::unique_ptr<kohzu::comm::Writer> writer;

    // callback worker queue entry
    struct CallbackTask {
        std::future<kohzu::protocol::Response> fut;
        MotorController::AsyncCallback cb;
        std::string key;
        int axis{-1};
    };

    std::mutex cbMtx;
    std::condition_variable cbCv;
    std::deque<CallbackTask> cbQueue;
    std::thread cbWorkerThread;
    bool cbWorkerRunning;
    std::atomic<bool> stopRequested;

    // spontaneous handlers will be registered into dispatcher; MotorController just forwards
    MotorController::OperationCallback onOperationStart;
    MotorController::OperationCallback onOperationFinish;

    // movement command set heuristic
    std::set<std::string> movementCommands{"APS", "MPS", "RPS"};

    // helper: build matching key for dispatcher
    static std::string makeKey(const std::string& cmd, const std::vector<std::string>& params) {
        std::string key = cmd;
        if (!params.empty() && !params[0].empty()) {
            key += ":" + params[0]; // assumes first param is axis
        }
        return key;
    }

    // parse axis from params[0] (returns -1 if not numeric)
    static int parseAxisFromParams(const std::vector<std::string>& params) {
        if (params.empty()) return -1;
        try {
            return std::stoi(params[0]);
        } catch (...) {
            return -1;
        }
    }
};

MotorController::MotorController(std::shared_ptr<kohzu::comm::ITcpClient> tcpClient,
                                 std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher)
    : impl_(std::make_unique<Impl>(std::move(tcpClient), std::move(dispatcher))) {
}

MotorController::~MotorController() {
    try {
        stop();
    } catch (...) {
        // swallow
    }
}

void MotorController::start() {
    // idempotent start
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx);
        if (impl_->cbWorkerRunning) return;
        impl_->stopRequested.store(false);
        impl_->cbWorkerRunning = true;
    }

    // create writer (uses ITcpClient::sendLine)
    if (!impl_->writer) {
        impl_->writer = std::make_unique<kohzu::comm::Writer>(impl_->tcpClient, 1000);
        impl_->writer->registerErrorHandler([this](std::exception_ptr ep){
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception &e) {
                std::cerr << "[MotorController::WriterError] " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[MotorController::WriterError] unknown exception\n";
            }
            impl_->dispatcher->cancelAllPendingWithException("Writer error: stopping motor controller");
        });
        impl_->writer->start();
    }

    // register tcp recv handler to feed Parser -> Dispatcher
    impl_->tcpClient->registerRecvHandler([this](const std::string& line) {
        // This runs in Asio io thread; keep it minimal
        auto resp = kohzu::protocol::Parser::parse(line);
        if (!resp.valid) {
            std::cerr << "[MotorController] Parser invalid line: " << line << std::endl;
            return;
        }

        // Build match key
        std::string key = resp.cmd;
        if (!resp.axis.empty()) key += ":" + resp.axis;

        // Try to fulfill pending; if not matched, treat as spontaneous
        bool matched = impl_->dispatcher->tryFulfill(key, resp);
        if (!matched) {
            impl_->dispatcher->notifySpontaneous(resp);
        }
    });

    // start callback worker thread
    impl_->cbWorkerThread = std::thread([this]() {
        while (true) {
            Impl::CallbackTask task;
            {
                std::unique_lock<std::mutex> lk(impl_->cbMtx);
                impl_->cbCv.wait(lk, [this]() { return impl_->stopRequested.load() || !impl_->cbQueue.empty(); });
                if (impl_->stopRequested.load() && impl_->cbQueue.empty()) break;
                if (impl_->cbQueue.empty()) continue;
                task = std::move(impl_->cbQueue.front());
                impl_->cbQueue.pop_front();
            }

            // Wait for the future to be ready (blocking inside callback worker)
            std::exception_ptr eptr = nullptr;
            kohzu::protocol::Response resp;
            try {
                resp = task.fut.get(); // may throw if promise set_exception
            } catch (...) {
                eptr = std::current_exception();
            }

            // Call user callback (if provided) with resp or exception ptr
            if (task.cb) {
                try {
                    task.cb(resp, eptr);
                } catch (const std::exception& e) {
                    std::cerr << "[MotorController] user async callback threw: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[MotorController] user async callback threw unknown exception\n";
                }
            }

            // Regardless of callback success or exception, call onOperationFinish if axis >= 0
            if (task.axis >= 0) {
                if (impl_->onOperationFinish) {
                    try {
                        impl_->onOperationFinish(task.axis);
                    } catch (...) {
                        // swallow to avoid killing worker
                        std::cerr << "[MotorController] onOperationFinish threw\n";
                    }
                }
            }
        } // end while

        // mark worker not running
        {
            std::lock_guard<std::mutex> lk(impl_->cbMtx);
            impl_->cbWorkerRunning = false;
        }
    });
}

void MotorController::stop() {
    // request stop for callback worker
    impl_->stopRequested.store(true);
    impl_->cbCv.notify_all();
    if (impl_->cbWorkerThread.joinable()) {
        impl_->cbWorkerThread.join();
    }
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx);
        impl_->cbWorkerRunning = false;
    }

    // stop writer
    if (impl_->writer) {
        impl_->writer->stop(true);
        impl_->writer.reset();
    }

    // cancel pending in dispatcher
    impl_->dispatcher->cancelAllPendingWithException("MotorController stopped");

    // clear recv handler by registering empty handler
    impl_->tcpClient->registerRecvHandler(nullptr);
}

void MotorController::connect(const std::string& host, uint16_t port) {
    impl_->tcpClient->connect(host, port);
}

bool MotorController::isConnected() const noexcept {
    return impl_->tcpClient->isConnected();
}

MotorController::Response MotorController::sendSync(const std::string& cmd,
                                                    const std::vector<std::string>& params,
                                                    std::chrono::milliseconds timeout) {
    if (!impl_->writer) throw std::runtime_error("Writer not started; call start() before sendSync");

    std::string key = Impl::makeKey(cmd, params);
    auto fut = impl_->dispatcher->addPending(key);

    std::string line = kohzu::protocol::CommandBuilder::makeCommand(cmd, params, false);
    try {
        // use blocking enqueue
        impl_->writer->enqueue(line);
    } catch (...) {
        // enqueue failed -> remove pending and rethrow
        impl_->dispatcher->removePendingWithException(key, "enqueue failed");
        throw;
    }

    // wait for response with timeout
    if (fut.wait_for(timeout) == std::future_status::ready) {
        return fut.get();
    } else {
        // timeout: remove pending and throw
        impl_->dispatcher->removePendingWithException(key, "timeout waiting for response");
        throw std::runtime_error("timeout waiting for response");
    }
}

std::future<MotorController::Response> MotorController::sendAsync(const std::string& cmd,
                                                                  const std::vector<std::string>& params) {
    if (!impl_->writer) throw std::runtime_error("Writer not started; call start() before sendAsync");

    std::string key = Impl::makeKey(cmd, params);
    auto fut = impl_->dispatcher->addPending(key);

    std::string line = kohzu::protocol::CommandBuilder::makeCommand(cmd, params, false);
    // non-throwing tryEnqueue fallback: if queue full, throw
    try {
        impl_->writer->enqueue(line);
    } catch (...) {
        impl_->dispatcher->removePendingWithException(key, "enqueue failed");
        throw;
    }

    return fut;
}

void MotorController::sendAsync(const std::string& cmd,
                                const std::vector<std::string>& params,
                                AsyncCallback cb) {
    if (!impl_->writer) throw std::runtime_error("Writer not started; call start() before sendAsync");

    std::string key = Impl::makeKey(cmd, params);
    auto fut = impl_->dispatcher->addPending(key);

    std::string line = kohzu::protocol::CommandBuilder::makeCommand(cmd, params, false);
    try {
        impl_->writer->enqueue(line);
    } catch (...) {
        impl_->dispatcher->removePendingWithException(key, "enqueue failed");
        if (cb) {
            cb(Response{}, std::make_exception_ptr(std::runtime_error("enqueue failed")));
        }
        return;
    }

    // If command looks like a movement command, call onOperationStart
    int axis = Impl::parseAxisFromParams(params);
    if (impl_->movementCommands.count(cmd) && axis >= 0) {
        if (impl_->onOperationStart) {
            try { impl_->onOperationStart(axis); } catch (...) { /* swallow */ }
        }
    }

    // push to callback queue
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx);
        impl_->cbQueue.push_back(Impl::CallbackTask{std::move(fut), cb, key, axis});
    }
    impl_->cbCv.notify_one();
}

void MotorController::registerSpontaneousHandler(SpontaneousHandler h) {
    impl_->dispatcher->registerSpontaneousHandler(std::move(h));
}

void MotorController::registerOperationCallbacks(OperationCallback onStart, OperationCallback onFinish) {
    impl_->onOperationStart = std::move(onStart);
    impl_->onOperationFinish = std::move(onFinish);
}

} // namespace kohzu::controller
