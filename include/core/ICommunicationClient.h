#pragma once

#include <string>
#include <functional>

/**
 * @interface ICommunicationClient
 * @brief 통신 클라이언트의 추상 인터페이스.
 * 비동기 데이터 송수신 기능을 포함하여 통신 계층의 결합도를 낮춥니다.
 */
class ICommunicationClient {
public:
    virtual ~ICommunicationClient() = default;

    /**
     * @brief 컨트롤러에 연결하는 메서드.
     * @param host 연결할 호스트 주소.
     * @param port 연결할 포트 번호.
     */
    virtual void connect(const std::string& host, const std::string& port) = 0;

    /**
     * @brief 데이터를 비동기적으로 전송하는 메서드.
     * @param data 전송할 문자열 데이터.
     */
    virtual void asyncWrite(const std::string& data) = 0;

    /**
     * @brief 데이터를 비동기적으로 수신하기 시작하는 메서드.
     * @param callback 수신 완료 시 호출될 콜백 함수.
     */
    virtual void asyncRead(std::function<void(const std::string&)> callback) = 0;
};
