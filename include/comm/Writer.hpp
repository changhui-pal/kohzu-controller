#pragma once

// include path: include/comm/Writer.hpp

#include <boost/asio.hpp>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace kohzu::comm {

// Synchronous writer running on a dedicated thread.
// - Construct with a connected socket reference
// - enqueue() will push a line (must include CRLF). If queue is too large, enqueue throws.
// - stop() requests shutdown and returns quickly; destructor joins the thread.
//
// Note: caller should not call blocking enqueue from GUI main thread.
//       Prefer worker threads or async wrappers.
class Writer {
public:
    // socket: reference to connected boost::asio::ip::tcp::socket
    // maxQueueSize: maximum number of queued messages before enqueue() fails (default 1000)
    explicit Writer(boost::asio::ip::tcp::socket& socket, std::size_t maxQueueSize = 1000);

    ~Writer();

    // Enqueue a line to send. The caller must include CRLF ("\r\n") at end of line.
    // Throws std::runtime_error if queue is full or if writer is stopped.
    void enqueue(const std::string& line);

    // Request orderly stop. Returns quickly. Destructor will join the thread.
    void stop();

    // Return number of queued items (for monitoring)
    std::size_t queuedSize() const;

private:
    void run(); // worker loop

    boost::asio::ip::tcp::socket& socket_;
    const std::size_t maxQueueSize_;

    mutable std::mutex mutex_;
    std::condition_variable condVar_;
    std::deque<std::string> queue_;
    bool stopRequested_;

    std::thread workerThread_;
};

} // namespace kohzu::comm

