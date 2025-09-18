#include "controller/KohzuController.h"
#include "controller/AxisState.h"
#include "core/TcpClient.h"
#include "protocol/ProtocolHandler.h"
#include "spdlog/spdlog.h"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <functional>

// Function prototypes
void handleUserInput(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::string& input);
void handleCommand(const std::string& command, const std::vector<std::string>& args,
                   const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState);
void handleApsCommand(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args);
void handleRpsCommand(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args);
void handleRdpCommand(const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args);
void handleStartMonitoringCommand(const std::shared_ptr<KohzuController>& controller, const std::vector<std::string>& args);

/**
 * @brief Main entry point of the application.
 */
int main() {
    // Set up logging level
    spdlog::set_level(spdlog::level::info);

    spdlog::info("Starting Kohzu controller project initialization.");
    try {
        // Boost.Asio I/O context and TcpClient setup
        boost::asio::io_context ioContext;
        auto client = std::make_shared<TcpClient>(ioContext, "192.168.1.120", "12321");
        spdlog::info("TcpClient object created: {}:{}", "192.168.1.120", "12321");

        // ProtocolHandler, AxisState, and KohzuController setup
        auto protocolHandler = std::make_shared<ProtocolHandler>(client);
        spdlog::info("ProtocolHandler object created.");
        auto axisState = std::make_shared<AxisState>();
        auto controller = std::make_shared<KohzuController>(protocolHandler, axisState);
        spdlog::info("KohzuController object created.");

        // Connect to the server
        client->connect("192.168.1.120", "12321");

        // Start the I/O context thread to run async operations
        std::thread ioThread([&ioContext]() {
            ioContext.run();
        });
        controller->start();

        // Main command loop
        std::cout << "Enter 'help' for available commands." << std::endl;
        std::string input;
        while (std::getline(std::cin, input) && input != "exit") {
            handleUserInput(controller, axisState, input);
        }

        controller->stopMonitoring();

        spdlog::info("Program exited gracefully.");
        ioContext.stop();
        if (ioThread.joinable()) {
            ioThread.join();
        }

    } catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        return 1;
    }

    return 0;
}

/**
 * @brief Handles user input by parsing the command and arguments.
 * @param controller Shared pointer to the KohzuController instance.
 * @param axisState Shared pointer to the AxisState instance.
 * @param input The user's input string.
 */
void handleUserInput(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::string& input) {
    std::stringstream ss(input);
    std::string command;
    ss >> command;

    std::vector<std::string> args;
    std::string arg;
    while (ss >> arg) {
        args.push_back(arg);
    }
    handleCommand(command, args, controller, axisState);
}

/**
 * @brief Dispatches the command to the appropriate handler function.
 * @param command The command string.
 * @param args A vector of command line arguments.
 * @param controller Shared pointer to the KohzuController instance.
 * @param axisState Shared pointer to the AxisState instance.
 */
void handleCommand(const std::string& command, const std::vector<std::string>& args,
                   const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState) {
    if (command == "aps") {
        handleApsCommand(controller, axisState, args);
    } else if (command == "rps") {
        handleRpsCommand(controller, axisState, args);
    } else if (command == "rdp") {
        handleRdpCommand(axisState, args);
    } else if (command == "start_monitor") {
        handleStartMonitoringCommand(controller, args);
    } else if (command == "help") {
        std::cout << "Available commands:\n"
                  << "  start_monitor [axis1] [axis2] ...\n"
                  << "  aps [axis_no] [position] [speed]\n"
                  << "  rps [axis_no] [distance] [speed]\n"
                  << "  rdp [axis_no] (reads from state cache)\n"
                  << "  exit\n";
    } else {
        std::cout << "Unknown command. Type 'help' for a list of commands.\n";
    }
}

/**
 * @brief Handles the 'aps' command for absolute positioning.
 * @param controller Shared pointer to the KohzuController instance.
 * @param axisState Shared pointer to the AxisState instance.
 * @param args A vector of command line arguments.
 */
void handleApsCommand(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args) {
    if (args.size() < 2 || args.size() > 3) {
        std::cout << "Usage: aps [axis_no] [position] [speed (optional)]\n";
        return;
    }
    try {
        int axisNo = std::stoi(args[0]);
        int position = std::stoi(args[1]);
        int speed = args.size() > 2 ? std::stoi(args[2]) : 0;
        std::cout << "Starting real-time monitoring for axis " << axisNo << "...\n";
        controller->startMonitoring({axisNo}, 100);
        controller->moveAbsolute(axisNo, position, speed, 0,
            [controller, axisNo, axisState](const ProtocolResponse& response) {
                if (response.status == 'C') {
                    spdlog::info("Absolute move command for axis {} completed.", axisNo);
                } else {
                    spdlog::error("Absolute move command for axis {} failed with status: {}", axisNo, response.status);
                }
                // Wait for the driving state to become 0 before stopping monitoring.
                while (axisState->getStatusDetails(axisNo).drivingState != 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                controller->stopMonitoring();
                std::cout << "Monitoring for axis " << axisNo << " stopped.\n";
            });
    } catch (const std::exception& e) {
        std::cout << "Invalid arguments. Please enter integers.\n";
    }
}

/**
 * @brief Handles the 'rps' command for relative positioning.
 * @param controller Shared pointer to the KohzuController instance.
 * @param axisState Shared pointer to the AxisState instance.
 * @param args A vector of command line arguments.
 */
void handleRpsCommand(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args) {
    if (args.size() < 2 || args.size() > 3) {
        std::cout << "Usage: rps [axis_no] [distance] [speed (optional)]\n";
        return;
    }
    try {
        int axisNo = std::stoi(args[0]);
        int distance = std::stoi(args[1]);
        int speed = args.size() > 2 ? std::stoi(args[2]) : 0;
        std::cout << "Starting real-time monitoring for axis " << axisNo << "...\n";
        controller->startMonitoring({axisNo}, 100);
        controller->moveRelative(axisNo, distance, speed, 0,
            [controller, axisNo, axisState](const ProtocolResponse& response) {
                if (response.status == 'C') {
                    spdlog::info("Relative move command for axis {} completed.", axisNo);
                } else {
                    spdlog::error("Relative move command for axis {} failed with status: {}", axisNo, response.status);
                }
                // Wait for the driving state to become 0 before stopping monitoring.
                while (axisState->getStatusDetails(axisNo).drivingState != 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                controller->stopMonitoring();
                std::cout << "Monitoring for axis " << axisNo << " stopped.\n";
            });
    } catch (const std::exception& e) {
        std::cout << "Invalid arguments. Please enter integers.\n";
    }
}

/**
 * @brief Handles the 'rdp' command to read cached position data.
 * @param axisState Shared pointer to the AxisState instance.
 * @param args A vector of command line arguments.
 */
void handleRdpCommand(const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::cout << "Usage: rdp [axis_no]\n";
        return;
    }
    try {
        int axisNo = std::stoi(args[0]);
        int position = axisState->getPosition(axisNo);
        AxisStatus status = axisState->getStatusDetails(axisNo);
        std::cout << "Current Position (cached): " << position << "\n";
        std::cout << "Current Status (cached):\n";
        std::cout << "  - Driving State: " << status.drivingState << "\n";
        std::cout << "  - EMG Signal: " << status.emgSignal << "\n";
        std::cout << "  - ORG/NORG Signal: " << status.orgNorgSignal << "\n";
        std::cout << "  - CW/CCW Limit: " << status.cwCcwLimitSignal << "\n";
        std::cout << "  - Soft Limit State: " << status.softLimitState << "\n";
        std::cout << "  - Correction Range: " << status.correctionAllowableRange << "\n";
    } catch (const std::exception& e) {
        std::cout << "Invalid argument. Please enter an integer.\n";
    }
}

/**
 * @brief Handles the 'start_monitor' command to start monitoring specific axes.
 * @param controller Shared pointer to the KohzuController instance.
 * @param args A vector of command line arguments.
 */
void handleStartMonitoringCommand(const std::shared_ptr<KohzuController>& controller, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: start_monitor [axis1] [axis2] ...\n";
        return;
    }

    std::vector<int> axes;
    try {
        for (const auto& arg : args) {
            axes.push_back(std::stoi(arg));
        }
    } catch (const std::exception& e) {
        std::cout << "Invalid arguments. Please enter integers for axis numbers.\n";
        return;
    }

    controller->startMonitoring(axes, 100); // Start monitoring with a 100ms period
}