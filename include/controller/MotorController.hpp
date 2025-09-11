#pragma once
/**
 * MotorController.hpp
 *
 * 상위 제어 레이어의 인터페이스 (리팩토링된 헤더).
 *
 * 목적:
 *  - ITcpClient/Dispatcher/Writer를 이용해 명령 전송 및 응답 처리 제공
 *  - start()/stop()으로 생명주기 제어
 *  - sendSync / sendAsync API 제공 (동기/비동기)
 *  - Spontaneous 메시지 핸들러 등록
 *  - Operation 시작/종료 콜백 등록 (Manager가 Poller 제어에 사용)
 *
 * 주의:
 *  - sendAsync의 콜백은 내부 워커 스레드(또는 dispatcher가 준비한 스레드)에서 호출될 수 있음.
 *    GUI 환경 등에서는 메인 스레드로 포워딩 필요.
 */

#include "../protocol/Parser.hpp"
#include "../protocol/Dispatcher.hpp"
#include "../comm/ITcpClient.hpp"
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace kohzu::controller {

class MotorController {
public:
    using Response = kohzu::protocol::Response;
    using AsyncCallback = std::function<void(const Response&, std::exception_ptr)>;
    using SpontaneousHandler = kohzu::protocol::Dispatcher::SpontaneousHandler;
    using OperationCallback = std::function<void(int axis)>;

    // 생성자: 의존성 주입을 권장 (tcpClient, dispatcher)
    MotorController(std::shared_ptr<kohzu::comm::ITcpClient> tcpClient,
                    std::shared_ptr<kohzu::protocol::Dispatcher> dispatcher);

    ~MotorController();

    // 경량 생성자: 실제 스레드/워커는 start()에서 시작
    void start();   // starts internal workers (e.g., callback worker) if any
    void stop();    // cancels pending, stops workers

    // 연결 제어: connect()는 tcpClient->connect를 호출
    void connect(const std::string& host, uint16_t port);

    bool isConnected() const noexcept;

    // sync send: throws on timeout/exception
    Response sendSync(const std::string& cmd,
                      const std::vector<std::string>& params,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(60000));

    // async send returning future
    std::future<Response> sendAsync(const std::string& cmd,
                                    const std::vector<std::string>& params);

    // async send with callback
    void sendAsync(const std::string& cmd,
                   const std::vector<std::string>& params,
                   AsyncCallback cb);

    // spontaneous handler registration (for events like EMG / SYS)
    void registerSpontaneousHandler(SpontaneousHandler h);

    // operation start/finish callbacks for manager to control poller
    void registerOperationCallbacks(OperationCallback onStart, OperationCallback onFinish);

private:
    // internal helpers (pimpl or private members in cpp)
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kohzu::controller