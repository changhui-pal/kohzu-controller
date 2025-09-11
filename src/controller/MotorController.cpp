#include "controller/MotorController.hpp"
#include "../protocol/CommandBuilder.hpp"
#include "../comm/Writer.hpp"

#include <iostream>
#include <thread>
#include <deque>
#include <set>

namespace kohzu::controller {

struct MotorController::Impl {
    Impl(std::shared_ptr<kohzu::comm::ITcpClient> tcp,
         std::shared_ptr<kohzu::protocol::Dispatcher> disp)
        : tcpClient(std::move(tcp)),
          dispatcher(std::move(disp)),
          writer(nullptr),
          stopRequested(false),
          cbWorkerRunning(false) {}

    std::shared_ptr<kohzu::comm::ITcpClient> tcpClient;
    std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher;
    std::unique_ptr<kohzu::comm::Writer> writer;

    std::mutex cbMtx;
    std::condition_variable cbCv;
    struct CallbackTask {
        std::future<kohzu::protocol::Response> fut;
        MotorController::AsyncCallback cb;
        std::string key;
        int axis{-1};
    };
    std::deque<CallbackTask> cbQueue;
    std::thread cbWorkerThread;
    std::atomic<bool> stopRequested;
    bool cbWorkerRunning;

    OperationCallback onOperationStart;
    OperationCallback onOperationFinish;

    // movement commands heuristic
    std::set<std::string> movementCommands{"APS","MPS","RPS","MOV","JOG"};
    static std::string makeKey(const std::string& cmd, const std::vector<std::string>& params) {
        if (!params.empty()) {
            return cmd + ":" + params[0];
        }
        return cmd + ":-1";
    }
    static int parseAxisFromParams(const std::vector<std::string>& params) {
        if (params.empty()) return -1;
        try { return std::stoi(params[0]); } catch (...) { return -1; }
    }
};

MotorController::MotorController(std::shared_ptr<kohzu::comm::ITcpClient> client,
                                 std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher)
    : impl_(std::make_unique<Impl>(client, dispatcher)) {}

MotorController::~MotorController() {
    try { stop(); } catch (...) {}
}

void MotorController::start() {
    // spawn cb worker if not running
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx);
        if (impl_->cbWorkerRunning) return;
        impl_->stopRequested.store(false);
        impl_->cbWorkerRunning = true;
    }

    if (!impl_->writer) {
        impl_->writer = std::make_unique<kohzu::comm::Writer>(impl_->tcpClient, kohzu::config::DEFAULT_WRITER_MAX_QUEUE);
        impl_->writer->registerErrorHandler([this](std::exception_ptr ep){
            try { if (ep) std::rethrow_exception(ep); }
            catch (const std::exception& e) { std::cerr << "[MotorController::WriterError] " << e.what() << std::endl; }
            catch (...) { std::cerr << "[MotorController::WriterError] unknown\n"; }
            if (impl_->dispatcher) {
                impl_->dispatcher->cancelAllPendingWithException("Writer error: stopping motor controller");
            }
        });
        impl_->writer->start();
    }

    // register recv handler for incoming lines
    impl_->tcpClient->registerRecvHandler([this](const std::string& line) {
        auto pr = kohzu::protocol::Parser::parse(line);
        if (!pr.valid) {
            std::cerr << "[MotorController] Parser invalid line: " << line << std::endl;
            return;
        }
        auto &resp = pr.resp;
        // key policy: command:axis  (axis numeric or -1)
        std::string key = resp.command + ":" + std::to_string(resp.axis);
        bool matched = false;
        try {
            if (impl_->dispatcher) matched = impl_->dispatcher->tryFulfill(key, resp);
        } catch (const std::exception& e) {
            std::cerr << "[MotorController] dispatcher.tryFulfill threw: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[MotorController] dispatcher.tryFulfill unknown\n";
        }
        if (!matched) {
            try {
                if (impl_->dispatcher) impl_->dispatcher->notifySpontaneous(resp);
            } catch (...) {
                std::cerr << "[MotorController] notifySpontaneous threw\n";
            }
        }
    });

    // register disconnect callback to cancel pending
    impl_->tcpClient->setOnDisconnect([disp = impl_->dispatcher]() {
        try { if (disp) disp->cancelAllPendingWithException("TCP disconnected"); } catch (...) {}
    });

    // start callback worker thread
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

            std::exception_ptr eptr = nullptr;
            kohzu::protocol::Response resp;
            try {
                resp = task.fut.get();
            } catch (...) {
                eptr = std::current_exception();
            }

            if (task.cb) {
                try {
                    std::thread([cb = task.cb, resp, eptr]() {
                        try { cb(resp, eptr); } catch (...) {}
                    }).detach();
                } catch (...) {
                    try { task.cb(resp, eptr); } catch (...) {}
                }
            }

            if (task.axis >= 0 && impl_->onOperationFinish) {
                try { impl_->onOperationFinish(task.axis); } catch (...) {}
            }
        }
        {
            std::lock_guard<std::mutex> lk(impl_->cbMtx);
            impl_->cbWorkerRunning = false;
        }
    });
}

void MotorController::stop() {
    impl_->stopRequested.store(true);
    impl_->cbCv.notify_all();
    if (impl_->cbWorkerThread.joinable()) {
        try { impl_->cbWorkerThread.join(); } catch (...) {}
    }
    if (impl_->writer) {
        impl_->writer->stop(true);
        impl_->writer.reset();
    }
    if (impl_->dispatcher) {
        impl_->dispatcher->cancelAllPendingWithException("MotorController stopped");
    }
    impl_->tcpClient->registerRecvHandler(nullptr);
    impl_->tcpClient->setOnDisconnect(nullptr);
}

std::future<MotorController::Response> MotorController::sendAsync(const std::string& cmd, const std::vector<std::string>& params) {
    if (!impl_->writer) throw std::runtime_error("Writer not started; call start()");
    std::string key = Impl::makeKey(cmd, params);
    auto fut = impl_->dispatcher->addPending(key);
    std::string line = kohzu::protocol::CommandBuilder::makeCommand(cmd, params, false);
    try {
        auto res = impl_->writer->enqueue(line);
        if (res != kohzu::comm::Writer::EnqueueResult::OK) {
            impl_->dispatcher->removePendingWithException(key, "enqueue failed");
            throw std::runtime_error("Writer enqueue failed");
        }
    } catch (...) {
        impl_->dispatcher->removePendingWithException(key, "enqueue exception");
        throw;
    }
    return fut;
}

MotorController::Response MotorController::sendSync(const std::string& cmd, const std::vector<std::string>& params, std::chrono::milliseconds timeout) {
    auto fut = sendAsync(cmd, params);
    if (fut.wait_for(timeout) == std::future_status::ready) {
        return fut.get();
    } else {
        std::string key = Impl::makeKey(cmd, params);
        impl_->dispatcher->removePendingWithException(key, "timeout waiting for response");
        throw std::runtime_error("timeout waiting for response");
    }
}

void MotorController::sendAsync(const std::string& cmd, const std::vector<std::string>& params, AsyncCallback cb) {
    auto fut = sendAsync(cmd, params); // above function
    int axis = Impl::parseAxisFromParams(params);
    {
        std::lock_guard<std::mutex> lk(impl_->cbMtx);
        impl_->cbQueue.push_back(Impl::CallbackTask{std::move(fut), cb, Impl::makeKey(cmd, params), axis});
    }
    impl_->cbCv.notify_one();

    if (axis >= 0 && impl_->movementCommands.count(cmd) && impl_->onOperationStart) {
        try { impl_->onOperationStart(axis); } catch (...) {}
    }
}

void MotorController::registerSpontaneousHandler(Dispatcher::SpontaneousHandler h) {
    if (impl_->dispatcher) impl_->dispatcher->registerSpontaneousHandler(std::move(h));
}

void MotorController::registerOperationCallbacks(OperationCallback onStart, OperationCallback onFinish) {
    impl_->onOperationStart = std::move(onStart);
    impl_->onOperationFinish = std::move(onFinish);
}

} // namespace kohzu::controller
