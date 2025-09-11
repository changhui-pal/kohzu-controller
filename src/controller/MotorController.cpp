// src/controller/MotorController.cpp
#include "controller/MotorController.hpp"
#include "protocol/CommandBuilder.hpp"
#include "protocol/Parser.hpp"
#include "comm/Writer.hpp"

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
    } catch (const std::exception& e) {
        std::cerr << "[MotorController::~] stop error: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[MotorController::~] stop unknown error\n";
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
            if (impl_->dispatcher) {
                try {
                    impl_->dispatcher->cancelAllPendingWithException("Writer error: stopping motor controller");
                } catch (...) {}
            }
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

        // If response looks like a reply to a pending request, try to fulfill one pending
        std::string key = resp.cmd;
        if (!resp.axis.empty()) {
            key += ":" + resp.axis;
        }
        bool matched = false;
        try {
            if (impl_->dispatcher) {
                matched = impl_->dispatcher->tryFulfill(key, resp);
            }
        } catch (const std::exception& e) {
            std::cerr << "[MotorController] dispatcher.tryFulfill threw: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[MotorController] dispatcher.tryFulfill unknown error\n";
        }

        if (!matched) {
            // If not matched, treat as spontaneous
            try {
                if (impl_->dispatcher) impl_->dispatcher->notifySpontaneous(resp);
            } catch (const std::exception& e) {
                std::cerr << "[MotorController] notifySpontaneous threw: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[MotorController] notifySpontaneous unknown error\n";
            }
        }
    });

    // register disconnect callback: when TCP disconnects, cancel pending
    impl_->tcpClient->setOnDisconnect([dispatcher = impl_->dispatcher]() {
        try {
            if (dispatcher) dispatcher->cancelAllPendingWithException("TCP disconnected");
        } catch (...) {}
    });

    // start callback worker thread (process futures and invoke callbacks)
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx);
        impl_->cbWorkerThread = std::thread([this]() {
            while (!impl_->stopRequested.load()) {
                Impl::CallbackTask task;
                {
                    std::unique_lock<std::mutex> lk(impl_->cbMtx);
                    impl_->cbCv.wait_for(lk, std::chrono::milliseconds(100), [this]() {
                        return impl_->stopRequested.load() || !impl_->cbQueue.empty();
                    });
                    if (impl_->stopRequested.load() && impl_->cbQueue.empty()) break;
                    if (!impl_->cbQueue.empty()) {
                        task = std::move(impl_->cbQueue.front());
                        impl_->cbQueue.pop_front();
                    } else {
                        continue;
                    }
                }

                // Wait for the future to be ready (blocking) but with exception safety
                std::exception_ptr eptr = nullptr;
                kohzu::protocol::Response resp;
                try {
                    resp = task.fut.get();
                } catch (...) {
                    eptr = std::current_exception();
                }

                // Call the user callback (in separate thread to avoid blocking worker if callback is slow)
                if (task.cb) {
                    try {
                        std::thread([cb = task.cb, resp, eptr]() {
                            try {
                                cb(resp, eptr);
                            } catch (const std::exception& e) {
                                std::cerr << "[MotorController] user async callback threw: " << e.what() << std::endl;
                            } catch (...) {
                                std::cerr << "[MotorController] user async callback unknown exception\n";
                            }
                        }).detach();
                    } catch (...) {
                        // If thread creation fails, call in current worker thread (best-effort)
                        try {
                            task.cb(resp, eptr);
                        } catch (...) { /* swallow */ }
                    }
                }

                // If this task was a movement start, optionally notify finish on error/complete via onOperationFinish
                if (task.axis >= 0 && impl_->onOperationFinish) {
                    try {
                        impl_->onOperationFinish(task.axis);
                    } catch (...) { /* swallow to avoid worker death */ }
                }
            }

            // mark worker not running
            {
                std::lock_guard<std::mutex> lk(impl_->cbMtx);
                impl_->cbWorkerRunning = false;
            }
        });
    }
}

void MotorController::stop() {
    // request stop for callback worker
    impl_->stopRequested.store(true);
    impl_->cbCv.notify_all();
    if (impl_->cbWorkerThread.joinable()) {
        try {
            impl_->cbWorkerThread.join();
        } catch (const std::exception& e) {
            std::cerr << "[MotorController::stop] cbWorkerThread join error: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[MotorController::stop] cbWorkerThread join unknown error\n";
        }
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
    if (impl_->dispatcher) {
        impl_->dispatcher->cancelAllPendingWithException("MotorController stopped");
    }

    // clear recv handler by registering empty handler
    impl_->tcpClient->registerRecvHandler(nullptr);

    // clear disconnect callback
    impl_->tcpClient->setOnDisconnect(nullptr);
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
    } catch (const std::exception& e) {
        // enqueue failed -> remove pending and rethrow
        impl_->dispatcher->removePendingWithException(key, "enqueue failed");
        std::cerr << "[MotorController::sendSync] enqueue error: " << e.what() << "\n";
        throw;
    } catch (...) {
        impl_->dispatcher->removePendingWithException(key, "enqueue unknown failed");
        throw std::runtime_error("[MotorController::sendSync] enqueue unknown error");
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
    } catch (const std::exception& e) {
        impl_->dispatcher->removePendingWithException(key, "enqueue failed");
        std::cerr << "[MotorController::sendAsync] enqueue error: " << e.what() << "\n";
        throw;
    } catch (...) {
        impl_->dispatcher->removePendingWithException(key, "enqueue unknown failed");
        throw std::runtime_error("[MotorController::sendAsync] enqueue unknown error");
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
    } catch (const std::exception& e) {
        impl_->dispatcher->removePendingWithException(key, "enqueue failed");
        std::cerr << "[MotorController::sendAsync cb] enqueue error: " << e.what() << "\n";
        if (cb) {
            cb(Response{}, std::make_exception_ptr(std::runtime_error("enqueue failed")));
        }
        return;
    } catch (...) {
        impl_->dispatcher->removePendingWithException(key, "enqueue unknown failed");
        std::cerr << "[MotorController::sendAsync cb] enqueue unknown error\n";
        if (cb) {
            cb(Response{}, std::make_exception_ptr(std::runtime_error("enqueue unknown failed")));
        }
        return;
    }

    // If command looks like a movement command, call onOperationStart
    int axis = Impl::parseAxisFromParams(params);
    if (impl_->movementCommands.count(cmd) && axis >= 0) {
        if (impl_->onOperationStart) {
            try { impl_->onOperationStart(axis); } catch (const std::exception& e) {
                std::cerr << "[MotorController::sendAsync] onOperationStart error: " << e.what() << "\n";
            } catch (...) { std::cerr << "[MotorController::sendAsync] onOperationStart unknown error\n"; }
        }
    }

    // push to callback queue for worker
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
