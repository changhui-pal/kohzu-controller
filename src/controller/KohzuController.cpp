#include "controller/KohzuController.h"
#include "spdlog/spdlog.h"
#include <stdexcept>
#include <chrono>

KohzuController::KohzuController(std::shared_ptr<ProtocolHandler> handler)
    : protocolHandler_(handler) {
    if (!protocolHandler_) {
        throw std::invalid_argument("ProtocolHandler가 유효하지 않습니다.");
    }
    spdlog::info("KohzuController 객체가 생성되었습니다.");
}

void KohzuController::start() {
    spdlog::info("KohzuController 시작.");
    protocolHandler_->initialize();
    // TODO: 컨트롤러 초기화 로직 (예: 원점 복귀 등) 추가
}

void KohzuController::moveAbsolute(int axis_no, int position, int speed, int response_type) {
    // APS 파라미터 순서: axis_no/speed/position/response_type
    std::string cmd = "APS" + std::to_string(axis_no) + "/" + std::to_string(speed) + "/" + std::to_string(position) + "/" + std::to_string(response_type);

    if (response_type == 0) {
        // 응답 타입이 0이면 동작 완료 콜백을 등록
        protocolHandler_->sendCommand(
            cmd,
            [this, axis_no](const ProtocolResponse& response) {
                spdlog::info("축 {} 절대 위치 이동 동작 완료.", axis_no);
            }
        );
    } else {
        // 응답 타입이 1이면 명령 송신 후 즉시 콜백 실행
        protocolHandler_->sendCommand(
            cmd,
            [this, axis_no](const ProtocolResponse& response) {
                spdlog::info("축 {} 절대 위치 이동 시작.", axis_no);
            }
        );
    }
}

void KohzuController::moveRelative(int axis_no, int distance, int speed, int response_type) {
    // RPS 파라미터 순서: axis_no/speed/distance/response_type
    std::string cmd = "RPS" + std::to_string(axis_no) + "/" + std::to_string(speed) + "/" + std::to_string(distance) + "/" + std::to_string(response_type);

    if (response_type == 0) {
        // 응답 타입이 0이면 동작 완료 콜백을 등록
        protocolHandler_->sendCommand(
            cmd,
            [this, axis_no](const ProtocolResponse& response) {
                spdlog::info("축 {} 상대 위치 이동 동작 완료.", axis_no);
            }
        );
    } else {
        // 응답 타입이 1이면 명령 송신 후 즉시 콜백 실행
        protocolHandler_->sendCommand(
            cmd,
            [this, axis_no](const ProtocolResponse& response) {
                spdlog::info("축 {} 상대 위치 이동 시작.", axis_no);
            }
        );
    }
}

void KohzuController::readCurrentPosition(int axis_no) {
    std::string cmd = "RDP" + std::to_string(axis_no);
    protocolHandler_->sendCommand(
        cmd,
        [axis_no](const ProtocolResponse& response) {
            if (response.status == 'C' && !response.params.empty()) {
                spdlog::info("축 {} 현재 위치: {}", axis_no, response.params[0]);
            } else {
                spdlog::error("축 {} RDP 명령에 대한 유효하지 않은 응답.", axis_no);
            }
        }
    );
}

void KohzuController::startPositionMonitor(int axis_no) {
    // TODO: 주기적인 RDP 명령을 보내고 콜백으로 위치를 갱신하는 로직을 추가해야 합니다.
    // 이는 Boost.Asio 타이머를 사용하거나 별도의 스레드에서 sleep을 사용하는 방법으로 구현할 수 있습니다.
    spdlog::info("축 {} 위치 모니터링 시작 (기능 구현 필요).", axis_no);
}
