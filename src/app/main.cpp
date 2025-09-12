#include "controller/KohzuController.h"
#include "core/TcpClient.h"
#include "protocol/ProtocolHandler.h"
#include "spdlog/spdlog.h"
#include "protocol/exceptions/ConnectionException.h"
#include "protocol/exceptions/ProtocolException.h"
#include "protocol/exceptions/TimeoutException.h"
#include <iostream>
#include <memory>
#include <thread>
#include <stdexcept>

int main() {
    // spdlog 설정
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Kohzu 컨트롤러 제어 프로젝트 시작.");

    // Boost.Asio I/O 컨텍스트 생성
    boost::asio::io_context io_context;
    boost::asio::io_context::work work(io_context);

    // I/O 컨텍스트를 전용 스레드에서 실행
    std::thread io_thread([&io_context]() {
        io_context.run();
    });

    try {
        // 의존성 객체 생성 및 주입
        std::shared_ptr<TcpClient> client = std::make_shared<TcpClient>(io_context, "127.0.0.1", "5000"); // TODO: IP 및 포트 설정 필요
        std::shared_ptr<ProtocolHandler> protocolHandler = std::make_shared<ProtocolHandler>(client);
        std::shared_ptr<KohzuController> controller = std::make_shared<KohzuController>(protocolHandler);

        // TCP 연결 시작
        client->connect("127.0.0.1", "5000"); // TODO: IP 및 포트 설정 필요

        // 컨트롤러 로직 시작
        controller->start();

        // 사용자 입력을 받아 명령을 실행하는 간단한 루프
        std::string input;
        while (std::cout << "명령을 입력하세요 (예: pos 1): " && std::getline(std::cin, input)) {
            if (input == "exit") {
                break;
            }

            if (input.rfind("pos", 0) == 0) {
                try {
                    int axis_no = std::stoi(input.substr(4));
                    std::string position = controller->readCurrentPosition(axis_no);
                    spdlog::info("현재 위치: {}", position);
                } catch (const std::exception& e) {
                    spdlog::error("오류 발생: {}", e.what());
                }
            } else if (input.rfind("moveabs", 0) == 0) {
                 // TODO: moveAbsolute 명령에 대한 사용자 입력 로직 구현
                 spdlog::warn("moveabs 명령은 아직 구현되지 않았습니다.");
            }
            // TODO: 다른 명령어에 대한 로직 추가
        }
        
    } catch (const std::exception& e) {
        spdlog::critical("치명적인 오류 발생: {}", e.what());
    }

    // 프로그램 종료 시 자원 해제
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    spdlog::info("프로그램이 정상적으로 종료되었습니다.");

    return 0;
}
