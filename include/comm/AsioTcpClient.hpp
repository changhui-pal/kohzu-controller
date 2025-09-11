#pragma once

/**
 * AsioTcpClient.hpp
 *
 * Boost.Asio 기반 ITcpClient 구현 (헤더)
 *
 * 변경/설계 요약:
 *  - 생성자에서는 io_context/소켓 생성까지만 수행(실제 io thread는 start()에서 실행)
 *  - connect(): 동기적으로 endpoint에 연결 시도(실제 실패 시 예외)
 *  - start(): 백그라운드 스레드에서 io_context.run()을 실행하고 async_read_until로 수신 시작
 *  - stop()/disconnect(): 안전 정리 (io_context.stop, thread join, socket close)
 *  - registerRecvHandler(): 수신된 라인(CRLF 제거)을 콜백에 전달
 *  - sendLine(): strand 또는 post 기반으로 async_write를 수행(스레드-안전)
 *
 * 주의:
 *  - 이 헤더는 구현(.cpp)에 비동기 로직을 두고, 여기서는 API/멤버 선언 및 동작 문서를 제공함.
 *  - 자발 메시지는 매우 드물게 발생하므로(EMG/ERROR), recv handler는 간단한 로깅/큐잉으로 상위에 전달하는 패턴을 권장.
 */

#include "ITcpClient.hpp"

#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace kohzu::comm {

class AsioTcpClient : public ITcpClient {
public:
    // ctor: io_context는 내부 소유. 필요시 외부 io_context 주입으로 변경 가능.
    AsioTcpClient();
    ~AsioTcpClient() override;

    // 비동기/동기 연계 정책:
    // - connect(): 동기 연결(예외 던짐)
    // - start(): 백그라운드에서 io_context.run() 시작
    void connect(const std::string& host, uint16_t port) override;
    void disconnect() override;

    void start() override;
    void stop() override;

    bool isConnected() const noexcept override;

    void registerRecvHandler(RecvHandler handler) override;

    void sendLine(const std::string& line) override;

private:
    // 비공개: 구현(.cpp)에서 정의
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kohzu::comm