#include "controller/KohzuController.h"
#include "spdlog/spdlog.h"
#include "protocol/exceptions/ConnectionException.h"
#include "protocol/exceptions/ProtocolException.h"
#include "protocol/exceptions/TimeoutException.h"

KohzuController::KohzuController(std::shared_ptr<ProtocolHandler> protocolHandler)
    : protocolHandler_(protocolHandler) {
    if (!protocolHandler_) {
        spdlog::error("ProtocolHandler is not valid.");
        throw std::invalid_argument("ProtocolHandler cannot be null.");
    }
    spdlog::info("KohzuController 객체가 생성되었습니다.");
}

void KohzuController::start() {
    spdlog::info("컨트롤러 로직 시작.");
    protocolHandler_->initialize();
    // The connection logic is handled in main.cpp, so no separate start logic is needed here.
    // However, if initialization is required, protocolHandler_->initialize() can be called.
}

void KohzuController::moveAbsolute(int axis_no, int position, int speed, int response_type) {
    try {
        protocolHandler_->moveAbsolute(axis_no, position, speed, response_type);
        spdlog::info("절대 위치 이동 명령 전송: 축 {}, 위치 {}, 속도 {}, 응답 타입 {}", axis_no, position, speed, response_type);
    } catch (const ProtocolException& e) {
        spdlog::error("moveAbsolute 명령 실패: {}", e.what());
    } catch (const ConnectionException& e) {
        spdlog::error("moveAbsolute 명령 실패: {}", e.what());
    }
}

void KohzuController::moveRelative(int axis_no, int distance, int speed, int response_type) {
    try {
        protocolHandler_->moveRelative(axis_no, distance, speed, response_type);
        spdlog::info("상대 위치 이동 명령 전송: 축 {}, 거리 {}, 속도 {}, 응답 타입 {}", axis_no, distance, speed, response_type);
    } catch (const ProtocolException& e) {
        spdlog::error("moveRelative 명령 실패: {}", e.what());
    } catch (const ConnectionException& e) {
        spdlog::error("moveRelative 명령 실패: {}", e.what());
    }
}

std::string KohzuController::readCurrentPosition(int axis_no) {
    try {
        spdlog::info("현재 위치 판독 명령 전송: 축 {}", axis_no);
        return protocolHandler_->readPosition(axis_no);
    } catch (const ConnectionException& e) {
        spdlog::error("readCurrentPosition 명령 실패: {}", e.what());
        throw; // Re-throw the exception to the higher-level caller
    } catch (const ProtocolException& e) {
        spdlog::error("readCurrentPosition 명령 실패: {}", e.what());
        throw; // Re-throw the exception to the higher-level caller
    } catch (const TimeoutException& e) {
        spdlog::error("readCurrentPosition 명령 실패: {}", e.what());
        throw; // Re-throw the exception to the higher-level caller
    }
}
