#include "protocol/ProtocolHandler.h"
#include "spdlog/spdlog.h"
#include "protocol/exceptions/ConnectionException.h"
#include "protocol/exceptions/ProtocolException.h"
#include "protocol/exceptions/TimeoutException.h"
#include <iostream>
#include <boost/lexical_cast.hpp>

ProtocolHandler::ProtocolHandler(std::shared_ptr<ICommunicationClient> client)
    : client_(client) {
    if (!client_) {
        throw std::invalid_argument("ICommunicationClient 객체가 유효하지 않습니다.");
    }
}

void ProtocolHandler::initialize() {
    spdlog::info("ProtocolHandler 초기화 시작.");
    // 비동기 수신 루프 시작
    startReceive();
}

void ProtocolHandler::sendCommand(const std::string& command, std::function<void(const ProtocolResponse&)> callback) {
    if (command.empty()) {
        spdlog::warn("빈 명령어를 전송할 수 없습니다.");
        return;
    }

    std::string full_command = command + "\r\n";
    std::string command_name = command.substr(0, 3);

    // 콜백 등록
    if (callback) {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        responseCallbacks_[command_name] = callback;
    }

    client_->asyncWrite(full_command, [command_name](const boost::system::error_code& error, size_t bytes_transferred) {
        if (!error) {
            spdlog::debug("명령어 '{}' 전송 완료: {} 바이트", command_name, bytes_transferred);
        } else {
            spdlog::error("명령어 '{}' 전송 실패: {}", command_name, error.message());
        }
    });
}

void ProtocolHandler::startReceive() {
    client_->asyncRead([this](const boost::system::error_code& error, const std::string& data) {
        if (!error) {
            spdlog::debug("데이터 수신: {}", data);
            try {
                ProtocolResponse response = parseResponse(data);
                handleResponse(response);
            } catch (const ProtocolException& e) {
                spdlog::error("프로토콜 오류: {}", e.what());
            }
            // 다음 비동기 수신을 시작
            startReceive();
        } else if (error == boost::asio::error::eof) {
            spdlog::warn("서버 연결이 끊어졌습니다.");
            // TODO: 연결 끊김 처리 (재연결 시도 등)
        } else {
            spdlog::error("읽기 오류: {}", error.message());
        }
    });
}

ProtocolResponse ProtocolHandler::parseResponse(const std::string& raw_data) {
    ProtocolResponse res;
    // 응답 형식: S,CMD,param1,param2... 또는 C,CMD,param1...
    // 예: C,RDP,0000001000
    std::stringstream ss(raw_data);
    std::string segment;
    std::vector<std::string> segments;

    while (std::getline(ss, segment, ',')) {
        segments.push_back(segment);
    }

    if (segments.empty()) {
        throw ProtocolException("응답 형식이 올바르지 않습니다: 응답 세그먼트가 없습니다.");
    }

    res.status = segments[0][0];
    if (segments.size() > 1) {
        res.command = segments[1];
    }
    if (segments.size() > 2) {
        // 첫 번째 파라미터가 축 번호일 경우
        try {
            res.axis_no = boost::lexical_cast<int>(segments[2]);
        } catch (const boost::bad_lexical_cast& e) {
            // 파라미터가 숫자가 아니거나, 축 번호가 아닌 경우
        }
        for (size_t i = 2; i < segments.size(); ++i) {
            res.params.push_back(segments[i]);
        }
    }

    return res;
}

void ProtocolHandler::handleResponse(const ProtocolResponse& response) {
    std::lock_guard<std::mutex> lock(callbackMutex_);
    if (responseCallbacks_.count(response.command)) {
        responseCallbacks_[response.command](response);
        // 일회용 콜백은 삭제
        responseCallbacks_.erase(response.command);
    } else {
        spdlog::warn("명령 {}에 대한 등록된 콜백이 없습니다. 무시.", response.command);
    }
}
