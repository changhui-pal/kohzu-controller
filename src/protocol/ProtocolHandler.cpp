#include "protocol/ProtocolHandler.h"
#include "spdlog/spdlog.h"
#include <stdexcept>
#include <sstream>
#include <boost/asio.hpp>
#include <atomic>

/**
 * @brief ProtocolHandler 클래스의 생성자.
 * @param client 통신 클라이언트 객체.
 */
ProtocolHandler::ProtocolHandler(std::shared_ptr<ICommunicationClient> client)
    : client_(client) {
    if (!client_) {
        throw std::invalid_argument("ICommunicationClient 객체가 유효하지 않습니다.");
    }
    spdlog::info("ProtocolHandler 객체가 생성되었습니다.");
}

/**
 * @brief 프로토콜 핸들러를 초기화하고 비동기 읽기 작업을 시작합니다.
 */
void ProtocolHandler::initialize() {
    if (!is_reading_) {
        is_reading_ = true;
        client_->asyncRead([this](const std::string& data) {
            this->handleRead(data);
        });
    }
}

/**
 * @brief 명령어를 컨트롤러로 전송하는 비동기 메서드.
 * @param command 전송할 명령어.
 * @param callback 응답이 도착했을 때 실행될 콜백 함수.
 */
void ProtocolHandler::sendCommand(const std::string& command, std::function<void(const ProtocolResponse&)> callback) {
    std::string response_key = command; // 예시로 명령어만 사용
    response_callbacks_[response_key] = callback;

    client_->asyncWrite(command + "\r\n");
}

/**
 * @brief 수신된 응답 데이터를 처리하는 메서드.
 * @param response_data 수신된 응답 문자열.
 */
void ProtocolHandler::handleRead(const std::string& response_data) {
    try {
        ProtocolResponse response = parseResponse(response_data);
        spdlog::info("응답 수신: {}", response.full_response);

        // 콜백 ID가 파라미터에 포함되어 있다고 가정
        if (response.params.empty()) {
            throw ProtocolException("응답에 콜백 ID가 포함되어 있지 않습니다.");
        }
        
        unsigned int request_id = std::stoul(response.params.back()); // 마지막 파라미터가 ID
        
        auto it = response_callbacks_.find(request_id);
        if (it != response_callbacks_.end()) {
            it->second(response);
            response_callbacks_.erase(it);
        } else {
            spdlog::warn("일치하는 콜백 ID를 찾을 수 없습니다. 응답: {}", response_data);
        }
    } catch (const ProtocolException& e) {
        spdlog::error("프로토콜 오류: {}", e.what());
    }
    client_->asyncRead([this](const std::string& data) {
        this->handleRead(data);
    });
}

/**
 * @brief 응답 문자열을 파싱하여 ProtocolResponse 구조체로 변환하는 메서드.
 * @param response 파싱할 응답 문자열.
 * @return 파싱된 ProtocolResponse 객체.
 */
ProtocolHandler::ProtocolResponse ProtocolHandler::parseResponse(const std::string& response) {
    ProtocolResponse parsed;
    parsed.full_response = response;
    std::string cleaned_response = response;

    // 캐리지 리턴과 라인 피드 제거
    if (!cleaned_response.empty() && cleaned_response.back() == '\n') {
        cleaned_response.pop_back();
    }
    if (!cleaned_response.empty() && cleaned_response.back() == '\r') {
        cleaned_response.pop_back();
    }
    
    if (cleaned_response.empty()) {
        throw ProtocolException("빈 응답 수신.");
    }

    parsed.status = cleaned_response[0];
    cleaned_response = cleaned_response.substr(1);

    size_t slash_pos = cleaned_response.find('/');
    if (slash_pos == std::string::npos) {
        parsed.command = cleaned_response;
        return parsed;
    }

    parsed.command = cleaned_response.substr(0, slash_pos);
    cleaned_response = cleaned_response.substr(slash_pos + 1);

    slash_pos = cleaned_response.find('/');
    if (slash_pos != std::string::npos) {
        try {
            parsed.axis_no = std::stoi(cleaned_response.substr(0, slash_pos));
            cleaned_response = cleaned_response.substr(slash_pos + 1);
        } catch (const std::exception& e) {
            throw ProtocolException("축 번호 파싱 실패: " + std::string(e.what()));
        }
    }

    std::stringstream ss(cleaned_response);
    std::string param;
    while (std::getline(ss, param, '/')) {
        parsed.params.push_back(param);
    }
    
    return parsed;
}
