#pragma once

/**
 * ITcpClient.hpp
 *
 * 통신 추상 인터페이스
 *
 * 변경: 연결 끊김 콜백(setOnDisconnect)을 기본 제공 API로 추가했습니다.
 */

#include <functional>
#include <string>
#include <cstdint>
#include <chrono>

namespace kohzu::comm {

class ITcpClient {
public:
    using RecvHandler = std::function<void(const std::string& line)>; // CRLF-제거된 라인

    virtual ~ITcpClient() = default;

    /// 동기적으로 서버에 연결을 시도한다. 실패 시 예외(예: std::runtime_error)를 던질 수 있음.
    virtual void connect(const std::string& host, uint16_t port) = 0;

    /// 연결을 끊고 내부 리소스를 정리한다. stop()이 내부에서 호출될 수 있음.
    virtual void disconnect() = 0;

    /// start/stop: io thread 제어(구현체에 따라 noop 가능)
    virtual void start() = 0;
    virtual void stop() = 0;

    /// 연결 여부 조회 (예: TCP 연결이 열려 있으면 true)
    virtual bool isConnected() const noexcept { return false; }

    /// register recv handler (line 단위)
    virtual void registerRecvHandler(RecvHandler handler) = 0;

    /// sendLine: 라인 단위 전송 API (CRLF는 구현체가 보장)
    virtual void sendLine(const std::string& line) = 0;

    /**
     * setOnDisconnect:
     * - 연결이 끊기거나 치명적인 IO 에러가 발생했을 때 호출될 콜백을 등록합니다.
     * - 기본 구현은 noop(아무것도 하지 않음)으로, 구현체가 필요시 오버라이드 합니다.
     *
     * 상위 계층(MotorController, KohzuManager 등)은 이 콜백에서 Dispatcher의 pending을 정리하도록 등록해야 합니다.
     */
    virtual void setOnDisconnect(std::function<void()> cb) {
        (void)cb;
    }
};

} // namespace kohzu::comm
