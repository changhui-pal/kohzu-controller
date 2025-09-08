// src/comm/Writer.cpp
#include "comm/Writer.hpp"
#include <boost/asio/write.hpp>
#include <iostream>
#include <stdexcept>

namespace kc = kohzu::comm;

kc::Writer::Writer(boost::asio::ip::tcp::socket& socket, std::size_t maxQueueSize)
: socket_(socket),
  maxQueueSize_(maxQueueSize),
  stopRequested_(false),
  workerThread_()
{
    // start worker thread
    workerThread_ = std::thread([this]() { run(); });
}

kc::Writer::~Writer() {
    stop();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void kc::Writer::enqueue(const std::string& line) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopRequested_) {
            throw std::runtime_error("Writer is stopped; cannot enqueue");
        }
        if (queue_.size() >= maxQueueSize_) {
            throw std::runtime_error("Writer queue full");
        }
        queue_.push_back(line);
    }
    condVar_.notify_one();
}

void kc::Writer::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    condVar_.notify_one();
}

std::size_t kc::Writer::queuedSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void kc::Writer::run() {
    while (true) {
        std::string item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condVar_.wait(lock, [this]() {
                return stopRequested_ || !queue_.empty();
            });

            if (stopRequested_ && queue_.empty()) {
                return;
            }

            // pop front
            item = std::move(queue_.front());
            queue_.pop_front();
        }

        try {
            // sync write (blocks until all bytes sent or error)
            boost::asio::write(socket_, boost::asio::buffer(item));
        } catch (const std::exception& e) {
            std::cerr << "Writer write error: " << e.what() << std::endl;
            // on fatal write error we stop the writer to avoid spinning
            // real app should notify controller to reconnect and clear pending requests
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopRequested_ = true;
            }
            condVar_.notify_one();
            return;
        }
    }
}

