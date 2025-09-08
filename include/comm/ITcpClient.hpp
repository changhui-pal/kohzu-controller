#pragma once
#include <functional>
#include <string>
#include <cstdint>
#include <boost/asio.hpp>

namespace kohzu::comm {

class ITcpClient {
public:
    using RecvHandler = std::function<void(const std::string&)>;

    virtual ~ITcpClient() = default;

    // Connect (blocking). Might throw on error.
    virtual void connect(const std::string& host, uint16_t port) = 0;

    // Close connection and stop internal threads.
    virtual void close() = 0;

    // Register callback that receives each complete line (CRLF-delimited, CR stripped).
    virtual void registerRecvHandler(RecvHandler cb) = 0;

    // Access to underlying socket (for Writer later). Implementation may throw if not connected.
    virtual boost::asio::ip::tcp::socket& socket() = 0;
};

} // namespace kohzu::comm

