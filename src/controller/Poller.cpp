#include "controller/Poller.hpp"
#include "controller/MotorController.hpp"
#include <iostream>

namespace kohzu::controller {

Poller::Poller(std::shared_ptr<MotorController> motor,
               std::shared_ptr<StateCache> cache,
               const std::vector<int>& axes,
               ms pollInterval,
               ms fastPollInterval)
    : motor_(std::move(motor)),
      cache_(std::move(cache)),
      axesOrder_(axes),
      pollInterval_(pollInterval),
      fastPollInterval_(fastPollInterval) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(axesMtx_);
    for (int a : axesOrder_) lastPolled_[a] = now - pollInterval_;
}

Poller::~Poller() {
    stop();
}

void Poller::start() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (running_) return;
    running_ = true;
    worker_ = std::thread(&Poller::runLoop, this);
}

void Poller::stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!running_) return;
        running_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        inflightRdp_.clear();
    }
}

void Poller::setAxes(const std::vector<int>& axes) {
    std::lock_guard<std::mutex> lk(axesMtx_);
    axesOrder_ = axes;
    auto now = std::chrono::steady_clock::now();
    for (int a : axesOrder_) {
        if (lastPolled_.find(a) == lastPolled_.end()) lastPolled_[a] = now - pollInterval_;
    }
}

void Poller::addAxis(int axis) {
    std::lock_guard<std::mutex> lk(axesMtx_);
    if (std::find(axesOrder_.begin(), axesOrder_.end(), axis) == axesOrder_.end()) {
        axesOrder_.push_back(axis);
        lastPolled_[axis] = std::chrono::steady_clock::now() - pollInterval_;
    }
}

void Poller::removeAxis(int axis) {
    {
        std::lock_guard<std::mutex> lk(axesMtx_);
        axesOrder_.erase(std::remove(axesOrder_.begin(), axesOrder_.end(), axis), axesOrder_.end());
        lastPolled_.erase(axis);
    }
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        inflightRdp_.erase(axis);
    }
    {
        std::lock_guard<std::mutex> lk(activeMtx_);
        activeAxes_.erase(axis);
    }
}

void Poller::notifyOperationStarted(int axis) {
    {
        std::lock_guard<std::mutex> lk(activeMtx_);
        activeAxes_.insert(axis);
    }
    scheduleRdp(axis);
    cv_.notify_all();
}

void Poller::notifyOperationFinished(int axis) {
    {
        std::lock_guard<std::mutex> lk(activeMtx_);
        activeAxes_.erase(axis);
    }

    // final synchronous reads to ensure up-to-date status
    try {
        auto rdpResp = motor_->sendSync("RDP", { std::to_string(axis) }, std::chrono::milliseconds(5000));
        if (rdpResp.valid && !rdpResp.params.empty()) {
            try {
                long pos = std::stol(rdpResp.params[0]);
                if (cache_) cache_->updatePosition(axis, pos);
            } catch (...) {
                if (cache_) cache_->updateRaw(axis, rdpResp.raw);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[Poller] notifyOperationFinished RDP error axis " << axis << " : " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Poller] notifyOperationFinished RDP unknown error axis " << axis << std::endl;
    }

    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        inflightRdp_.erase(axis);
    }
}

void Poller::scheduleRdp(int axis) {
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        if (inflightRdp_.count(axis)) return;
    }
    try {
        auto fut = motor_->sendAsync("RDP", { std::to_string(axis) });
        std::lock_guard<std::mutex> lk(inflightMtx_);
        inflightRdp_.emplace(axis, fut.share());
    } catch (const std::exception& e) {
        std::cerr << "[Poller] scheduleRdp sendAsync failed axis " << axis << " : " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[Poller] scheduleRdp unknown error axis " << axis << std::endl;
    }
}

void Poller::handleCompletedInflight() {
    std::vector<int> finished;
    {
        std::lock_guard<std::mutex> lk(inflightMtx_);
        for (auto &kv : inflightRdp_) {
            if (kv.second.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                finished.push_back(kv.first);
            }
        }
    }
    for (int axis : finished) {
        std::shared_future<kohzu::protocol::Response> sf;
        {
            std::lock_guard<std::mutex> lk(inflightMtx_);
            auto it = inflightRdp_.find(axis);
            if (it == inflightRdp_.end()) continue;
            sf = it->second;
            inflightRdp_.erase(it);
        }
        try {
            auto resp = sf.get();
            if (!resp.valid) {
                std::cerr << "[Poller] invalid RDP response axis " << axis << " raw=" << resp.raw << std::endl;
                continue;
            }
            if (!resp.params.empty()) {
                try {
                    long pos = std::stol(resp.params[0]);
                    if (cache_) cache_->updatePosition(axis, pos);
                } catch (...) {
                    if (cache_) cache_->updateRaw(axis, resp.raw);
                }
            } else {
                if (cache_) cache_->updateRaw(axis, resp.raw);
            }
        } catch (const std::exception& e) {
            std::cerr << "[Poller] inflight get exception axis " << axis << " : " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[Poller] inflight get unknown exception axis " << axis << std::endl;
        }
    }
}

void Poller::runLoop() {
    while (true) {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (!running_) break;
        }

        handleCompletedInflight();

        std::vector<int> axesCopy;
        {
            std::lock_guard<std::mutex> lk(axesMtx_);
            axesCopy = axesOrder_;
        }
        auto now = std::chrono::steady_clock::now();
        for (int axis : axesCopy) {
            bool isActive = false;
            {
                std::lock_guard<std::mutex> lk(activeMtx_);
                isActive = (activeAxes_.find(axis) != activeAxes_.end());
            }
            ms desired = isActive ? fastPollInterval_ : pollInterval_;
            auto lastIt = lastPolled_.find(axis);
            auto last = (lastIt != lastPolled_.end()) ? lastIt->second : (now - desired);
            if (now - last >= desired) {
                {
                    std::lock_guard<std::mutex> lk(inflightMtx_);
                    if (inflightRdp_.count(axis) == 0) {
                        scheduleRdp(axis);
                        lastPolled_[axis] = now;
                    }
                }
            }
        }

        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait_for(lk, std::chrono::milliseconds(50), [this]() { return !running_; });
    }
    handleCompletedInflight();
}

} // namespace kohzu::controller
