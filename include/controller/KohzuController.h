#pragma once

#include "protocol/ProtocolHandler.h"
#include "protocol/exceptions/ConnectionException.h"
#include "protocol/exceptions/ProtocolException.h"
#include "protocol/exceptions/TimeoutException.h"
#include <memory>

/**
 * @class KohzuController
 * @brief KOHZU 컨트롤러의 고수준 제어를 위한 인터페이스를 제공하는 클래스.
 * ProtocolHandler를 사용하여 실제 통신을 수행합니다.
 */
class KohzuController {
public:
    /**
     * @brief KohzuController 클래스의 생성자.
     * @param handler ProtocolHandler 객체의 공유 포인터.
     */
    explicit KohzuController(std::shared_ptr<ProtocolHandler> handler);

    /**
     * @brief 컨트롤러 초기화를 시작하는 메서드.
     */
    void start();

    /**
     * @brief 지정된 축을 절대 위치로 이동시키는 메서드.
     * @param axis_no 축 번호.
     * @param position 절대 위치 값.
     * @param speed 이동 속도.
     * @param response_type 응답 타입 (0: 동작 완료 후 응답, 1: 명령 수신 후 즉시 응답).
     */
    void moveAbsolute(int axis_no, int position, int speed, int response_type);

    /**
     * @brief 지정된 축을 상대 거리만큼 이동시키는 메서드.
     * @param axis_no 축 번호.
     * @param distance 상대 이동 거리.
     * @param speed 이동 속도.
     * @param response_type 응답 타입 (0: 동작 완료 후 응답, 1: 명령 수신 후 즉시 응답).
     */
    void moveRelative(int axis_no, int distance, int speed, int response_type);

    /**
     * @brief 지정된 축의 현재 위치를 읽고 로그를 출력하는 비동기 메서드.
     * @param axis_no 축 번호.
     */
    void readCurrentPosition(int axis_no);

private:
    std::shared_ptr<ProtocolHandler> protocolHandler_;
};
