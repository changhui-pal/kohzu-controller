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
 * @brief Displays a list of available commands.
 */
void showHelp() {
    std::cout << "Available commands:\n";
    std::cout << "  aps <axis_no> <position> <speed> <response_type>  - Move to absolute position\n";
    std::cout << "  rps <axis_no> <distance> <speed> <response_type>  - Move relative to current position\n";
    std::cout << "  rdp <axis_no>                                     - Read current position\n";
    std::cout << "  cerr                                              - Read the last controller error\n";
    std::cout << "  exit                                              - Exit the program\n";
    std::cout << "  help                                              - Show this help message\n";
}

/**
 * @brief Handles the 'aps' command.
 * @param iss Input stream
 * @param controller KohzuController object
 */
void handleApsCommand(std::istringstream& iss, const std::shared_ptr<KohzuController>& controller) {
    int axis_no, position, speed, response_type;
    if (iss >> axis_no >> position >> speed >> response_type) {
        controller->moveAbsolute(axis_no, position, speed, response_type);
    } else {
        spdlog::error("Invalid aps command format. Usage: aps <axis_no> <position> <speed> <response_type>");
    }
}

/**
 * @brief Handles the 'rps' command.
 * @param iss Input stream
 * @param controller KohzuController object
 */
void handleRpsCommand(std::istringstream& iss, const std::shared_ptr<KohzuController>& controller) {
    int axis_no, distance, speed, response_type;
    if (iss >> axis_no >> distance >> speed >> response_type) {
        controller->moveRelative(axis_no, distance, speed, response_type);
    } else {
        spdlog::error("Invalid rps command format. Usage: rps <axis_no> <distance> <speed> <response_type>");
    }
}

/**
 * @brief Handles the 'rdp' command.
 * @param iss Input stream
 * @param controller KohzuController object
 */
void handleRdpCommand(std::istringstream& iss, const std::shared_ptr<KohzuController>& controller) {
    int axis_no;
    if (iss >> axis_no) {
        controller->readCurrentPosition(axis_no);
    } else {
        spdlog::error("Invalid rdp command format. Usage: rdp <axis_no>");
    }
}

/**
 * @brief Handles the 'cerr' command.
 * @param iss Input stream
 * @param controller KohzuController object
 */
void handleCerrCommand(const std::shared_ptr<KohzuController>& controller) {
    controller->readLastError();
}

int main() {
    // Spdlog setup
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("Starting Kohzu controller project initialization.");

    boost::asio::io_context io_context;
    boost::asio::io_context::work work(io_context);
    std::thread io_thread([&io_context]() { io_context.run(); });

    std::shared_ptr<TcpClient> client;
    std::shared_ptr<ProtocolHandler> protocolHandler;
    std::shared_ptr<KohzuController> controller;

    try {
        // Create and inject dependency objects
        client = std::make_shared<TcpClient>(io_context, "192.168.1.120", "12321");
        protocolHandler = std::make_shared<ProtocolHandler>(client);
        controller = std::make_shared<KohzuController>(protocolHandler);

        // Start TCP connection
        client->connect("192.168.1.120", "12321");

        // Start controller logic
        controller->start();

    } catch (const std::exception& e) {
        spdlog::critical("Fatal error during initialization: {}", e.what());
        // If an exception occurs, stop io_context to terminate the thread.
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }
        return 1;
    }

    // A simple loop to take user input and execute commands
    std::string input;
    std::cout << "Enter 'help' for available commands.\n";
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
            } else if (command == "cerr") {
                handleCerrCommand(controller);
            } else {
                spdlog::warn("Unknown command. Enter 'help' to see available commands.");
            }
        } catch (const std::exception& e) {
            spdlog::error("An error occurred: {}", e.what());
        }
    }

    // Release resources upon program exit
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    spdlog::info("Program exited gracefully.");

    return 0;
}
