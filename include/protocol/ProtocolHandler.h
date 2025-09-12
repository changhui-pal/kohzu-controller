#pragma once

#include "core/ICommunicationClient.h"
#include "common/ThreadSafeQueue.h"
#include "protocol/exceptions/TimeoutException.h"
#include "protocol/exceptions/ProtocolException.h"
#include <memory>
#include <string>
#include <vector>
#include <future>
#include <map>
#include <functional>

/**
 * @struct ProtocolResponse
 * @brief 컨트롤러 응답을 파싱하여 저장하는 구조체.
 */
struct ProtocolResponse {
    char status;
    std::string command;
    std::vector<std::string> params;
    int axis_no;
};

/**
 * @class ProtocolHandler
 * @brief KOHZU 컨트롤러의 프로토콜을 처리하는 클래스.
 *
 * 이 클래스는 저수준의 TCP 통신 계층을 추상화하고,
 * 명령어 송수신 및 응답 파싱과 같은 프로토콜 관련 로직을 캡슐화합니다.
 */
class ProtocolHandler {
public:
    /**
     * @brief 생성자. 통신 클라이언트 객체를 주입받습니다.
     * @param client ICommunicationClient 객체에 대한 공유 포인터.
     */
    explicit ProtocolHandler(std::shared_ptr<ICommunicationClient> client);

    /**
     * @brief 컨트롤러와의 연결을 초기화합니다.
     */
    void initialize();

    /**
     * @brief 명령어를 비동기적으로 전송하고, 응답을 처리할 콜백을 등록합니다.
     * @param command 전송할 명령어 문자열.
     * @param callback 응답이 도착했을 때 호출될 콜백 함수.
     */
    void sendCommand(const std::string& command, std::function<void(const ProtocolResponse&)> callback);

private:
    /**
     * @brief TCP 클라이언트로부터 데이터를 비동기적으로 수신합니다.
     */
    void startReceive();

    /**
     * @brief 수신된 데이터를 파싱하여 ProtocolResponse 객체로 변환합니다.
     * @param raw_data 수신된 원시 데이터.
     * @return 파싱된 ProtocolResponse 객체.
     */
    ProtocolResponse parseResponse(const std::string& raw_data);

    /**
     * @brief 파싱된 응답에 대한 콜백을 실행합니다.
     * @param response 파싱된 ProtocolResponse 객체.
     */
    void handleResponse(const ProtocolResponse& response);

private:
    std::shared_ptr<ICommunicationClient> client_;
    std::map<std::string, std::function<void(const ProtocolResponse&)>> responseCallbacks_;
    std::mutex callbackMutex_;
};
