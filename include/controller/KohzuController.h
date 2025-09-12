#pragma once

#include "protocol/ProtocolHandler.h"
#include <memory>
#include <string>
#include <functional>

/**
 * @class KohzuController
 * @brief KOHZU 컨트롤러의 고수준 제어 API를 제공하고 다축 비동기 동작을 관리하는 클래스.
 *
 * 이 클래스는 ProtocolHandler를 사용하여 복잡한 프로토콜 세부 사항을 추상화하고,
 * 사용자 친화적인 메서드를 제공합니다.
 */
class KohzuController {
public:
    /**
     * @brief 생성자. ProtocolHandler 객체를 주입받습니다.
     * @param handler ProtocolHandler 객체에 대한 공유 포인터.
     */
    explicit KohzuController(std::shared_ptr<ProtocolHandler> handler);

    /**
     * @brief 컨트롤러 초기화 및 시작.
     */
    void start();

    /**
     * @brief 지정된 축을 절대 위치로 이동시킵니다.
     * @param axis_no 이동할 축 번호 (1-3).
     * @param position 목표 절대 위치.
     * @param speed 이동 속도.
     * @param response_type 응답 타입 (0: 완료 후 응답, 1: 즉시 응답).
     */
    void moveAbsolute(int axis_no, int position, int speed, int response_type);

    /**
     * @brief 지정된 축을 상대 위치로 이동시킵니다.
     * @param axis_no 이동할 축 번호 (1-3).
     * @param distance 이동할 상대 거리.
     * @param speed 이동 속도.
     * @param response_type 응답 타입 (0: 완료 후 응답, 1: 즉시 응답).
     */
    void moveRelative(int axis_no, int distance, int speed, int response_type);

    /**
     * @brief 지정된 축의 현재 위치를 비동기적으로 읽어옵니다.
     * @param axis_no 위치를 읽어올 축 번호 (1-3).
     */
    void readCurrentPosition(int axis_no);

    /**
     * @brief 현재 위치를 주기적으로 모니터링하기 시작합니다.
     * @param axis_no 모니터링할 축 번호.
     */
    void startPositionMonitor(int axis_no);
};
