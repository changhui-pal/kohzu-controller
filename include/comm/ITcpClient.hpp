#pragma once

/**
 * ITcpClient.hpp
 *
 * 통신 추상 인터페이스 (리팩토링 버전)
 *
 * 핵심 포인트:
 *  - low-level socket 노출 제거 (socket() 제거)
 *  - connect()/disconnect() : 연결 수립/종료
 *  - start()/stop() : io_context 백그라운드 실행과 정지 (생명주기 제어)
 *  - registerRecvHandler(...) : CRLF 단위로 수신된 라인 콜백 등록
 *  - sendLine(...) : 안전한 라인 단위 송신 API (Writer와의 통합 또는 대체용)
 *
 * 설계 의도:
 *  - 생성자에서는 스레드/IO를 시작하지 않음. start()/stop()로 명확히 제어.
 *  - sendLine은 스레드 안전해야 하며, 내부적으로 async_write/strand 또는 동기 write(또는 예외)로 구현됨.
 *  - 자발 메시지는 매우 드물게 발생하므로(EMG 등), 수신 핸들러에서 간단히 로깅/큐잉 후 상위에서 처리하는 방식을 권장.
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

    /**
     * start(): 내부 io_context의 run()을 백그라운드 스레드에서 시작하고,
     *          수신 비동기 루프(async_read_until("\r\n"))를 활성화한다.
     *
     * stop(): 백그라운드 스레드를 정지하고, 모든 비동기 작업을 중단/정리한다.
     *
     * 설계상 connect() 후 start()를 호출하는 흐름을 권장한다.
     */
    virtual void start() = 0;
    virtual void stop() = 0;

    /// 현재 연결 상태를 스레드-안전하게 반환
    virtual bool isConnected() const noexcept = 0;

    /**
     * recv handler 등록
     * - 인자로 전달되는 문자열은 CRLF가 제거된 '한 라인'입니다.
     * - 호출 스레드: 기본 구현에서는 Asio io 스레드(수신 콜백 쓰레드)에서 호출됩니다.
     *   따라서 핸들러 내부에서 무거운 처리를 할 경우 별도 스레드/큐로 offload 하세요.
     */
    virtual void registerRecvHandler(RecvHandler handler) = 0;

    /**
     * 안전한 라인 전송 API
     * - line은 CRLF 미포함 또는 포함 모두 허용하되, 구현체가 적절히 CRLF를 붙여 전송함.
     * - 호출은 스레드-안전해야 함.
     * - 전송 실패 시 예외(예: std::runtime_error) 또는 실패 상태 반환(구현에 따름).
     */
    virtual void sendLine(const std::string& line) = 0;
};

} // namespace kohzu::comm