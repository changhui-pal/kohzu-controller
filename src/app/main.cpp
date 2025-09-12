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
#include <string>
#include <vector>
#include <sstream>
#include <boost/asio.hpp>

/**
 * @brief 사용 가능한 명령어를 화면에 표시하는 함수
 */
void showHelp() {
    std::cout << "사용 가능한 명령어:\n";
    std::cout << "  aps <축번호> <위치> <속도> <응답타입>  - 절대 위치로 이동\n";
    std::cout << "  rps <축번호> <거리> <속도> <응답타입>  - 상대 위치로 이동\n";
    std::cout << "  rdp <축번호>                           - 현재 위치 읽기\n";
    std::cout << "  exit                                     - 프로그램 종료\n";
    std::cout << "  help                                     - 도움말 표시\n";
}

/**
 * @brief 'aps' 명령어를 처리하는 함수
 * @param iss 입력 스트림
 * @param controller KohzuController 객체
 */
void handleApsCommand(std::istringstream& iss, const std::shared_ptr<KohzuController>& controller) {
    int axis_no, position, speed, response_type;
    if (iss >> axis_no >> position >> speed >> response_type) {
        controller->moveAbsolute(axis_no, position, speed, response_type);
    } else {
        spdlog::error("잘못된 aps 명령어 형식. 사용법: aps <축번호> <위치> <속도> <응답타입>");
    }
}

/**
 * @brief 'rps' 명령어를 처리하는 함수
 * @param iss 입력 스트림
 * @param controller KohzuController 객체
 */
void handleRpsCommand(std::istringstream& iss, const std::shared_ptr<KohzuController>& controller) {
    int axis_no, distance, speed, response_type;
    if (iss >> axis_no >> distance >> speed >> response_type) {
        controller->moveRelative(axis_no, distance, speed, response_type);
    } else {
        spdlog::error("잘못된 rps 명령어 형식. 사용법: rps <축번호> <거리> <속도> <응답타입>");
    }
}

/**
 * @brief 'rdp' 명령어를 처리하는 함수
 * @param iss 입력 스트림
 * @param controller KohzuController 객체
 */
void handleRdpCommand(std::istringstream& iss, const std::shared_ptr<KohzuController>& controller) {
    int axis_no;
    if (iss >> axis_no) {
        controller->readCurrentPosition(axis_no);
    } else {
        spdlog::error("잘못된 rdp 명령어 형식. 사용법: rdp <축번호>");
    }
}

int main() {
    // spdlog 설정
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Kohzu 컨트롤러 제어 프로젝트 초기화 시작.");

    boost::asio::io_context io_context;
    boost::asio::io_context::work work(io_context);
    std::thread io_thread([&io_context]() { io_context.run(); });

    std::shared_ptr<TcpClient> client;
    std::shared_ptr<ProtocolHandler> protocolHandler;
    std::shared_ptr<KohzuController> controller;

    try {
        // 의존성 객체 생성 및 주입
        client = std::make_shared<TcpClient>(io_context, "127.0.0.1", "5000");
        protocolHandler = std::make_shared<ProtocolHandler>(client);
        controller = std::make_shared<KohzuController>(protocolHandler);

        // TCP 연결 시작
        client->connect("127.0.0.1", "5000");

        // 컨트롤러 로직 시작
        controller->start();

    } catch (const std::exception& e) {
        spdlog::critical("초기화 중 치명적인 오류 발생: {}", e.what());
        // 예외가 발생하면 io_context를 중지하여 스레드가 종료되도록 함.
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }
        return 1;
    }

    // 사용자 입력을 받아 명령을 실행하는 간단한 루프
    std::string input;
    std::cout << "도움말을 보려면 'help'를 입력하세요.\n";
    while (std::cout << "> " && std::getline(std::cin, input)) {
        std::istringstream iss(input);
        std::string command;
        iss >> command;

        try {
            if (command == "exit") {
                break;
            } else if (command == "help") {
                showHelp();
            } else if (command == "aps") {
                handleApsCommand(iss, controller);
            } else if (command == "rps") {
                handleRpsCommand(iss, controller);
            } else if (command == "rdp") {
                handleRdpCommand(iss, controller);
            } else {
                spdlog::warn("알 수 없는 명령어입니다. 'help'를 입력하여 사용 가능한 명령어를 확인하세요.");
            }
        } catch (const std::exception& e) {
            spdlog::error("오류 발생: {}", e.what());
        }
    }

    // 프로그램 종료 시 자원 해제
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    spdlog::info("프로그램이 정상적으로 종료되었습니다.");

    return 0;
}
