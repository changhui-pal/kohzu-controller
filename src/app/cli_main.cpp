// src/app/cli_main.cpp
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>
#include <chrono>
#include <iomanip>
#include <sys/select.h>
#include <unistd.h>
#include <cerrno>

#include "controller/KohzuManager.hpp"

using namespace std::chrono_literals;
using namespace kohzu::controller;
using kohzu::protocol::Response;

static std::atomic<bool> g_stop{false};

void sigint_handler(int /*signum*/) {
    g_stop.store(true);
}

static std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string tok;
    while (iss >> tok) {
        out.push_back(tok);
    }
    return out;
}

static std::vector<int> parse_axis_list(const std::string& s) {
    std::vector<int> out;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, ',')) {
        // Trim whitespace
        size_t b = item.find_first_not_of(" \t");
        size_t e = item.find_last_not_of(" \t");
        if (b == std::string::npos) continue;
        std::string trimmed = item.substr(b, e - b + 1);
        try {
            int a = std::stoi(trimmed);
            if (a <= 0) {
                std::cerr << "[CLI] Warning: Skipping invalid axis " << trimmed << " (must be positive)\n";
                continue;
            }
            out.push_back(a);
        } catch (const std::exception& e) {
            std::cerr << "[CLI] Warning: Skipping invalid axis '" << trimmed << "': " << e.what() << "\n";
        }
    }
    return out;
}

bool attemptConnectWithPrompt(KohzuManager &manager, bool autoReconnect) {
    bool ok = manager.connectOnce();
    if (ok) {
        std::cout << "[CLI] Connected successfully to controller\n";
        return true;
    }

    if (autoReconnect) {
        std::cerr << "[CLI] connectOnce failed. Auto-reconnect enabled; manager will retry in background.\n";
        return false;
    }

    // Interactive retry prompt
    const int maxRetries = 5;
    int retryCount = 0;
    while (!ok && !g_stop.load() && retryCount < maxRetries) {
        std::cerr << "[CLI] connectOnce failed (attempt " << (retryCount + 1) << "/" << maxRetries << ").\n";
        std::cout << "Retry connection? (y/n): " << std::flush;
        std::string answer;
        if (!std::getline(std::cin, answer)) {
            if (g_stop.load()) {
                std::cerr << "[CLI] Input interrupted by signal, exiting connect attempt.\n";
            } else {
                std::cerr << "[CLI] Input error or EOF, exiting connect attempt.\n";
            }
            return false;
        }
        if (g_stop.load()) {
            std::cerr << "[CLI] Connection attempt aborted due to signal.\n";
            return false;
        }
        if (answer.empty() || answer[0] == 'y' || answer[0] == 'Y') {
            std::cout << "[CLI] Retrying connection...\n";
            ok = manager.connectOnce();
            if (ok) {
                std::cout << "[CLI] Connected successfully\n";
                return true;
            }
            retryCount++;
        } else {
            std::cerr << "[CLI] Connection attempt aborted by user.\n";
            return false;
        }
    }
    if (retryCount >= maxRetries) {
        std::cerr << "[CLI] Maximum retries (" << maxRetries << ") reached. Exiting connect attempt.\n";
    }
    return ok;
}

int main(int argc, char** argv) {
    // Default configuration
    std::string host = "192.168.1.120";
    uint16_t port = 12321;
    bool autoReconnect = false;

    // Parse command-line arguments
    if (argc >= 2) host = argv[1];
    if (argc >= 3) {
        try {
            int p = std::stoi(argv[2]);
            if (p < 1 || p > 65535) {
                std::cerr << "[CLI] Invalid port " << argv[2] << ", using default " << port << "\n";
            } else {
                port = static_cast<uint16_t>(p);
            }
        } catch (const std::exception& e) {
            std::cerr << "[CLI] Invalid port '" << argv[2] << "': " << e.what() << ", using default " << port << "\n";
        }
    }
    if (argc >= 4) {
        std::string ar = argv[3];
        autoReconnect = (ar == "1" || ar == "true" || ar == "yes");
    }

    // Initialize manager
    KohzuManager manager(host, port, autoReconnect);
    std::signal(SIGINT, sigint_handler);

    // Register spontaneous handler
    manager.registerSpontaneousHandler([](const Response& resp) {
        std::cout << "\n[SPONT] Raw: " << resp.raw << "\n";
        std::cout << "        Type=" << resp.type << " Cmd=" << resp.cmd;
        if (!resp.axis.empty()) std::cout << " Axis=" << resp.axis;
        if (!resp.params.empty()) {
            std::cout << " Params=[";
            for (size_t i = 0; i < resp.params.size(); ++i) {
                if (i) std::cout << ",";
                std::cout << resp.params[i];
            }
            std::cout << "]";
        }
        std::cout << "\n> " << std::flush;
    });

    // Connect or start manager
    if (autoReconnect) {
        std::cout << "[CLI] Starting manager with auto-reconnect ON\n";
        try {
            manager.startAsync();
        } catch (const std::exception& e) {
            std::cerr << "[CLI] Failed to start manager: " << e.what() << "\n";
            return 1;
        }
    } else {
        std::cout << "[CLI] Attempting single connection to " << host << ":" << port << " ...\n";
        if (!attemptConnectWithPrompt(manager, autoReconnect)) {
            try {
                manager.stop();
            } catch (const std::exception& e) {
                std::cerr << "[CLI] Stop error during failed connect: " << e.what() << "\n";
            }
            return 1;
        }
    }

    std::cout << "kohzu-controller CLI\n";
    std::cout << "Type 'help' for commands.\n> " << std::flush;

    // Main loop with select()
    const int STDIN_FD = fileno(stdin);
    std::string line;
    while (!g_stop.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FD, &readfds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms for responsiveness

        int rv = select(STDIN_FD + 1, &readfds, nullptr, nullptr, &tv);
        if (rv == -1) {
            if (errno == EINTR && g_stop.load()) {
                std::cerr << "[CLI] Interrupted by signal, exiting.\n";
                break;
            }
            std::cerr << "[CLI] select() error: " << strerror(errno) << "\n";
            continue;
        } else if (rv == 0) {
            continue; // Timeout, check g_stop again
        }

        if (FD_ISSET(STDIN_FD, &readfds)) {
            if (!std::getline(std::cin, line)) {
                if (g_stop.load()) {
                    std::cerr << "[CLI] Input interrupted by signal, exiting.\n";
                } else {
                    std::cerr << "[CLI] Input error or EOF, exiting.\n";
                }
                break;
            }
            if (line.empty()) {
                std::cout << "> " << std::flush;
                continue;
            }

            auto toks = split_ws(line);
            const std::string& cmd = toks[0];

            if (cmd == "help") {
                std::cout << "Commands:\n"
                          << "  help                        : Show this help\n"
                          << "  start                       : Start manager (auto-reconnect mode)\n"
                          << "  connect                     : Attempt single connection\n"
                          << "  move abs <axis> <pos>       : Move axis to absolute position (async)\n"
                          << "  poll set <a,b,c>            : Set poll axes list (comma-separated)\n"
                          << "  poll add <axis>             : Add a poll axis\n"
                          << "  poll rm <axis>              : Remove a poll axis\n"
                          << "  state                       : Print state cache snapshot\n"
                          << "  quit                        : Exit CLI\n";
            } else if (cmd == "start") {
                try {
                    manager.startAsync();
                    std::cout << "[CLI] Manager started with auto-reconnect\n";
                } catch (const std::exception& e) {
                    std::cerr << "[CLI] startAsync error: " << e.what() << "\n";
                }
            } else if (cmd == "connect") {
                bool ok = attemptConnectWithPrompt(manager, autoReconnect);
                std::cout << (ok ? "[CLI] Connection succeeded\n" : "[CLI] Connection failed or aborted\n");
            } else if (cmd == "move" && toks.size() >= 4 && toks[1] == "abs") {
                int axis = 0;
                long pos = 0;
                try {
                    axis = std::stoi(toks[2]);
                    if (axis <= 0) throw std::invalid_argument("Axis must be positive");
                } catch (const std::exception& e) {
                    std::cerr << "[CLI] Invalid axis '" << toks[2] << "': " << e.what() << "\n";
                    std::cout << "> " << std::flush;
                    continue;
                }
                try {
                    pos = std::stol(toks[3]);
                } catch (const std::exception& e) {
                    std::cerr << "[CLI] Invalid position '" << toks[3] << "': " << e.what() << "\n";
                    std::cout << "> " << std::flush;
                    continue;
                }

                KohzuManager::AsyncCallback cb = [axis](const Response& r, std::exception_ptr ep) {
                    if (ep) {
                        try {
                            std::rethrow_exception(ep);
                        } catch (const std::exception& e) {
                            std::cerr << "[MOVE cb] Axis " << axis << " error: " << e.what() << "\n";
                        } catch (...) {
                            std::cerr << "[MOVE cb] Axis " << axis << " unknown error\n";
                        }
                    } else {
                        std::cout << "[MOVE cb] Axis " << axis << " response: " << r.raw << "\n";
                    }
                    std::cout << "> " << std::flush;
                };

                try {
                    manager.moveAbsoluteAsync(axis, pos, cb);
                    std::cout << "[CLI] Move command dispatched (axis=" << axis << ", pos=" << pos << ")\n";
                } catch (const std::exception& e) {
                    std::cerr << "[CLI] moveAbsoluteAsync error: " << e.what() << "\n";
                }
            } else if (cmd == "poll" && toks.size() >= 3) {
                if (toks[1] == "set") {
                    std::string rest = line.substr(line.find("set") + 3);
                    auto axes = parse_axis_list(rest);
                    try {
                        manager.setPollAxes(axes);
                        std::cout << "[CLI] Poll axes set: ";
                        for (size_t i = 0; i < axes.size(); ++i) {
                            if (i) std::cout << ",";
                            std::cout << axes[i];
                        }
                        std::cout << "\n";
                    } catch (const std::exception& e) {
                        std::cerr << "[CLI] setPollAxes error: " << e.what() << "\n";
                    }
                } else if (toks[1] == "add") {
                    try {
                        int axis = std::stoi(toks[2]);
                        if (axis <= 0) throw std::invalid_argument("Axis must be positive");
                        manager.addPollAxis(axis);
                        std::cout << "[CLI] Added poll axis " << axis << "\n";
                    } catch (const std::exception& e) {
                        std::cerr << "[CLI] Invalid axis '" << toks[2] << "': " << e.what() << "\n";
                    }
                } else if (toks[1] == "rm" || toks[1] == "remove") {
                    try {
                        int axis = std::stoi(toks[2]);
                        if (axis <= 0) throw std::invalid_argument("Axis must be positive");
                        manager.removePollAxis(axis);
                        std::cout << "[CLI] Removed poll axis " << axis << "\n";
                    } catch (const std::exception& e) {
                        std::cerr << "[CLI] Invalid axis '" << toks[2] << "': " << e.what() << "\n";
                    }
                } else {
                    std::cerr << "[CLI] Usage: poll set <a,b,c> | poll add <axis> | poll rm <axis>\n";
                }
            } else if (cmd == "state") {
                try {
                    auto snap = manager.snapshotState();
                    if (snap.empty()) {
                        std::cout << "[CLI] State cache empty\n";
                    } else {
                        std::cout << "State snapshot (" << snap.size() << " axes):\n";
                        auto now = std::chrono::steady_clock::now();
                        for (const auto& kv : snap) {
                            int axis = kv.first;
                            const AxisState& st = kv.second;
                            std::cout << "  Axis " << axis << ": ";
                            if (st.position.has_value()) {
                                std::cout << "pos=" << *st.position << " ";
                            } else {
                                std::cout << "pos=N/A ";
                            }
                            if (st.running.has_value()) {
                                std::cout << "running=" << (*st.running ? "yes" : "no") << " ";
                            } else {
                                std::cout << "running=N/A ";
                            }
                            if (!st.raw.empty()) {
                                std::cout << "raw=\"" << st.raw << "\" ";
                            }
                            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.lastUpdated).count();
                            std::cout << "age=" << age << "ms\n";
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[CLI] snapshotState error: " << e.what() << "\n";
                }
            } else if (cmd == "quit" || cmd == "exit") {
                std::cout << "[CLI] Quitting...\n";
                break;
            } else {
                std::cerr << "[CLI] Unknown command: " << cmd << " (type 'help')\n";
            }
            std::cout << "> " << std::flush;
        }
    }

    // Graceful shutdown
    try {
        manager.stop();
        std::cout << "[CLI] Manager stopped\n";
    } catch (const std::exception& e) {
        std::cerr << "[CLI] Manager stop error: " << e.what() << "\n";
        return 1;
    }
    std::cout << "[CLI] Exited\n";
    return 0;
}
