#pragma once

#include "core/ICommunicationClient.h"
#include "common/ThreadSafeQueue.h"
#include "protocol/exceptions/ConnectionException.h"
#include "protocol/exceptions/ProtocolException.h"
#include "protocol/exceptions/TimeoutException.h"
#include <functional>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <future>
#include <atomic>

/**
 * @struct ProtocolResponse
 * @brief 프로토콜 응답을 구조화하기 위한 데이터 구조체.
 */
struct ProtocolResponse {
    char status;
    int axis_no;
    std::string command;
    std::vector<std::string> params;
    std::string full_response;
};

/**
 * @class ProtocolHandler
 * @brief KOHZU 컨트롤러와의 통신 프로토콜을 처리하는 클래스.
 * 명령어를 포맷팅하고 응답을 파싱하는 역할을 담당합니다.
 */
class ProtocolHandler {
public:
    /**
     * @brief ProtocolHandler 클래스의 생성자.
     * @param client 통신 클라이언트 객체의 공유 포인터.
     */
    explicit ProtocolHandler(std::shared_ptr<ICommunicationClient> client);

    /**
     * @brief 프로토콜 핸들러를 초기화하는 메서드.
     */
    void initialize();

    /**
     * @brief 명령어를 컨트롤러로 전송하는 비동기 메서드.
     * @param command 전송할 명령어.
     * @param callback 응답이 도착했을 때 실행될 콜백 함수.
     */
    void sendCommand(const std::string& command, std::function<void(const ProtocolResponse&)> callback);

private:
    void handleRead(const std::string& response_data);
    ProtocolResponse parseResponse(const std::string& response);

    std::shared_ptr<ICommunicationClient> client_;
    std::map<std::string, std::function<void(const ProtocolResponse&)>> response_callbacks_;
    std::atomic<bool> is_reading_ = false;
};
