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
        
        std::cout << "Starting real-time monitoring for axis " << axis_no << "...\n";
        controller->startMonitoring({axis_no}, 100);

        controller->moveAbsolute(axis_no, position, speed, 0,
            [controller, axis_no, axisState](const ProtocolResponse& response) {
                if (response.status == 'C') {
                    spdlog::info("Absolute move command for axis {} completed.", axis_no);
                } else {
                    spdlog::error("Absolute move command for axis {} failed with status: {}", axis_no, response.status);
                }
                // Wait for the driving state to become 0 before stopping monitoring.
                while (axisState->getStatusDetails(axis_no).driving_state != 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                controller->stopMonitoring();
                std::cout << "Monitoring for axis " << axis_no << " stopped.\n";
            });
            
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
        
        std::cout << "Starting real-time monitoring for axis " << axis_no << "...\n";
        controller->startMonitoring({axis_no}, 100);

        controller->moveRelative(axis_no, distance, speed, 0,
            [controller, axis_no, axisState](const ProtocolResponse& response) {
                if (response.status == 'C') {
                    spdlog::info("Relative move command for axis {} completed.", axis_no);
                } else {
                    spdlog::error("Relative move command for axis {} failed with status: {}", axis_no, response.status);
                }
                // Wait for the driving state to become 0 before stopping monitoring.
                while (axisState->getStatusDetails(axis_no).driving_state != 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                controller->stopMonitoring();
                std::cout << "Monitoring for axis " << axis_no << " stopped.\n";
            });
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
