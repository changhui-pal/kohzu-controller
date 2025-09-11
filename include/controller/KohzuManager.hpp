#pragma once
/**
 * KohzuManager.hpp (refactored)
 *
 * Manager that orchestrates MotorController, Poller and StateCache.
 *
 * Responsibilities:
 *  - create and manage MotorController lifecycle
 *  - create and manage Poller and StateCache
 *  - optionally run a reconnect loop (startAsync)
 *  - provide a simplified API for client usage (moveAbsoluteAsync, register handlers)
 *
 * Threading:
 *  - startAsync launches a background reconnection thread if autoReconnect==true
 *  - All public methods are thread-safe unless documented otherwise
 *
 * Note: This manager currently assumes a single device/controller. It is straightforward to
 * extend to multiple controllers by changing controller_ to a collection.
 */

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <optional>
#include <vector>

#include "MotorController.hpp"
#include "Poller.hpp"
#include "StateCache.hpp"
#include "../comm/AsioTcpClient.hpp"
#include "../protocol/Dispatcher.hpp"

namespace kohzu::controller {

class KohzuManager {
public:
    using ms = std::chrono::milliseconds;
    using SpontaneousHandler = kohzu::protocol::Dispatcher::SpontaneousHandler;
    using AsyncCallback = MotorController::AsyncCallback;

    KohzuManager(const std::string& host,
                 uint16_t port,
                 bool autoReconnect = false,
                 ms reconnectInterval = ms(5000),
                 ms pollInterval = ms(500),
                 ms fastPollInterval = ms(100));
    ~KohzuManager();

    // non-copyable
    KohzuManager(const KohzuManager&) = delete;
    KohzuManager& operator=(const KohzuManager&) = delete;

    // start manager: if autoReconnect==true, runs background reconnection thread
    // otherwise, attempts a single connect and returns (throws on failure)
    void startAsync(); // launches recon thread if configured
    void stop();

    // create controller (for now single controller) and attempt connect once
    // returns true on success
    bool connectOnce();

    // status
    bool isRunning() const noexcept;        // manager running (reconnect thread)
    bool isConnected() const noexcept;      // controller connected

    // high level commands
    // move absolute asynchronously: axis, position. Optional callback invoked when response arrives.
    void moveAbsoluteAsync(int axis, long position, AsyncCallback cb = nullptr);

    // register a handler for spontaneous messages (Manager will forward to controller)
    void registerSpontaneousHandler(SpontaneousHandler h);

    // Axis list management for Poller
    void setPollAxes(const std::vector<int>& axes);
    void addPollAxis(int axis);
    void removePollAxis(int axis);

    // For advanced use: notify operation start/finish (can be called by external code)
    void notifyOperationStarted(int axis);
    void notifyOperationFinished(int axis);

    // Get a snapshot of current state cache
    std::unordered_map<int, AxisState> snapshotState() const;

private:
    void reconnectionLoop();

    // host/port
    std::string host_;
    uint16_t port_;

    // options
    bool autoReconnect_;
    ms reconnectInterval_;
    ms pollInterval_;
    ms fastPollInterval_;

    // core owned components
    std::shared_ptr<kohzu::comm::AsioTcpClient> tcpClient_;
    std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher_;
    std::shared_ptr<MotorController> controller_;
    std::shared_ptr<StateCache> cache_;
    std::shared_ptr<Poller> poller_;

    // reconnection thread
    std::thread reconThread_;
    std::atomic<bool> running_{false};

    // synchronization
    mutable std::mutex mtx_;

    // helper to teardown/create controller/poller
    void teardown();
    void setupControllerAndPoller();

    // flag used to signal manual stop
    std::atomic<bool> stopRequested_{false};
};

} // namespace kohzu::controller