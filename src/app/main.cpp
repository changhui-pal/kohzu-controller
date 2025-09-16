#include "controller/KohzuController.h"
#include "controller/AxisState.h"
#include "core/TcpClient.h" // 추가: TcpClient 클래스 정의 포함
#include "protocol/ProtocolHandler.h" // 추가: ProtocolHandler 클래스 정의 포함
#include "spdlog/spdlog.h"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <future>
#include <stdexcept>
#include <sstream>
#include <algorithm>

// Function prototypes
void handleUserInput(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::string& input);
void handleCommand(const std::string& command, const std::vector<std::string>& args,
                   const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState);
void handleApsCommand(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args);
void handleRpsCommand(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args);
void handleRdpCommand(const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args);
void handleStartMonitoringCommand(const std::shared_ptr<KohzuController>& controller, const std::vector<std::string>& args);

// Function to print axis state during a command operation
void monitorAndPrint(const std::shared_ptr<AxisState>& axisState, int axis_no, std::future<void> future);

int main() {
    // Set up logging
    spdlog::set_level(spdlog::level::info);

    spdlog::info("Starting Kohzu controller project initialization.");

    try {
        // Boost.Asio I/O context and TcpClient setup
        boost::asio::io_context io_context;
        auto client = std::make_shared<TcpClient>(io_context, "192.168.1.120", "12321");
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
        std::thread io_thread([&io_context]() {
            io_context.run();
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
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }

    } catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        return 1;
    }

    return 0;
}

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

void handleApsCommand(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args) {
    if (args.size() < 2 || args.size() > 3) {
        std::cout << "Usage: aps [axis_no] [position] [speed (optional)]\n";
        return;
    }
    try {
        int axis_no = std::stoi(args[0]);
        int position = std::stoi(args[1]);
        int speed = args.size() > 2 ? std::stoi(args[2]) : 0;
        
        // Use a promise/future to know when the command completes
        std::promise<void> promise;
        std::future<void> future = promise.get_future();

        // Pass a callback to the controller that fulfills the promise
        controller->moveAbsolute(axis_no, position, speed, 0,
            [&promise](const ProtocolResponse& response) {
                if (response.status == 'C') {
                    spdlog::info("Absolute move command for axis {} completed.", response.axis_no);
                } else {
                    spdlog::error("Absolute move command for axis {} failed with status: {}", response.axis_no, response.status);
                }
                promise.set_value(); // Fulfill the promise to signal completion
            });
            
        // Start monitoring and printing position
        monitorAndPrint(axisState, axis_no, std::move(future));

    } catch (const std::exception& e) {
        std::cout << "Invalid arguments. Please enter integers.\n";
    }
}

void handleRpsCommand(const std::shared_ptr<KohzuController>& controller, const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args) {
    if (args.size() < 2 || args.size() > 3) {
        std::cout << "Usage: rps [axis_no] [distance] [speed (optional)]\n";
        return;
    }
    try {
        int axis_no = std::stoi(args[0]);
        int distance = std::stoi(args[1]);
        int speed = args.size() > 2 ? std::stoi(args[2]) : 0;
        
        // Use a promise/future to know when the command completes
        std::promise<void> promise;
        std::future<void> future = promise.get_future();

        // Pass a callback to the controller that fulfills the promise
        controller->moveRelative(axis_no, distance, speed, 0,
            [&promise](const ProtocolResponse& response) {
                if (response.status == 'C') {
                    spdlog::info("Relative move command for axis {} completed.", response.axis_no);
                } else {
                    spdlog::error("Relative move command for axis {} failed with status: {}", response.axis_no, response.status);
                }
                promise.set_value(); // Fulfill the promise to signal completion
            });

        // Start monitoring and printing position
        monitorAndPrint(axisState, axis_no, std::move(future));
    } catch (const std::exception& e) {
        std::cout << "Invalid arguments. Please enter integers.\n";
    }
}

void handleRdpCommand(const std::shared_ptr<AxisState>& axisState, const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::cout << "Usage: rdp [axis_no]\n";
        return;
    }
    try {
        int axis_no = std::stoi(args[0]);
        int position = axisState->getPosition(axis_no);
        AxisStatus status = axisState->getStatusDetails(axis_no);
        std::cout << "Current Position (cached): " << position << "\n";
        std::cout << "Current Status (cached):\n";
        std::cout << "  - Driving State: " << status.driving_state << "\n";
        std::cout << "  - EMG Signal: " << status.emg_signal << "\n";
        std::cout << "  - ORG/NORG Signal: " << status.org_norg_signal << "\n";
        std::cout << "  - CW/CCW Limit: " << status.cw_ccw_limit_signal << "\n";
        std::cout << "  - Soft Limit State: " << status.soft_limit_state << "\n";
        std::cout << "  - Correction Range: " << status.correction_allowable_range << "\n";
    } catch (const std::exception& e) {
        std::cout << "Invalid argument. Please enter an integer.\n";
    }
}

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

// Monitors axis position at a 100ms interval until the command completes
void monitorAndPrint(const std::shared_ptr<AxisState>& axisState, int axis_no, std::future<void> future) {
    std::cout << "Monitoring axis " << axis_no << " position..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    
    while (future.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        int current_position = axisState->getPosition(axis_no);
        std::cout << "  -> Position: " << current_position << "\n";
        // To avoid flooding the console, wait a bit before the next check.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Monitoring complete." << std::endl;
    std::cout << "Final position: " << axisState->getPosition(axis_no) << std::endl;
}
