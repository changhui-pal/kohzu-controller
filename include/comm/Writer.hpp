#pragma once
/**
 * Writer.hpp
 *
 * Threaded, bounded, line-oriented writer that sends lines via ITcpClient::sendLine().
 *
 * Design notes:
 * - Writer does NOT access sockets directly. It calls ITcpClient::sendLine(line).
 * - Construction is lightweight; IO worker thread starts on start() and stops on stop().
 * - enqueue(...) blocks until there is space (or until stop() is requested).
 * - tryEnqueue(...) attempts to push without blocking.
 * - stop(flush=true): if flush==true, worker drains the queue then exits; if false, worker exits asap and pending items remain.
 * - send exceptions from ITcpClient are caught and forwarded to a registered error handler (if any).
 *
 * Thread-safety:
 * - enqueue/tryEnqueue/queuedSize are thread-safe.
 * - registerErrorHandler should be called before start() to ensure no events are missed (but is thread-safe).
 */

#include "ITcpClient.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace kohzu::comm {

class Writer {
public:
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    /**
     * ctor
     * - client: shared_ptr to ITcpClient; Writer will call client->sendLine().
     * - maxQueueSize: maximum number of queued lines allowed (bounded queue).
     */
    Writer(std::shared_ptr<ITcpClient> client, std::size_t maxQueueSize = 1000);

    // non-copyable
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;

    ~Writer();

    /**
     * start the internal worker thread.
     * Must be called before enqueueing (or it's allowed to call; enqueue will block until start called).
     */
    void start();

    /**
     * stop the worker thread.
     * if flush==true (default) the worker will process all queued items before exiting.
     * if flush==false the worker will exit as soon as possible (pending items may remain).
     */
    void stop(bool flush = true);

    /**
     * enqueue (blocking):
     * - Blocks until there's space in the queue or until stop() is requested.
     * - Throws std::runtime_error if writer is stopped or destruction in progress.
     */
    void enqueue(const std::string& line);

    /**
     * tryEnqueue (non-blocking):
     * - If queue has room, pushes and returns true. Otherwise returns false.
     * - Does not throw on full queue.
     */
    bool tryEnqueue(const std::string& line);

    /**
     * queuedSize
     */
    std::size_t queuedSize() const noexcept;

    /**
     * register an error handler to be called when underlying send fails.
     * The handler is called with an exception_ptr from the caught exception (from ITcpClient->sendLine()).
     * The handler will be invoked on the internal writer worker thread.
     */
    void registerErrorHandler(ErrorHandler eh);

private:
    void workerLoop();

    std::shared_ptr<ITcpClient> client_;
    mutable std::mutex mtx_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::deque<std::string> queue_;
    const std::size_t maxQueueSize_;

    std::atomic<bool> running_;        // true while start() and worker running
    std::atomic<bool> stopRequested_;  // set by stop()

    std::thread workerThread_;
    ErrorHandler errorHandler_;
};

} // namespace kohzu::comm
