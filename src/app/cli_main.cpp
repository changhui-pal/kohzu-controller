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

#include "controller/KohzuManager.hpp"

using namespace std::chrono_literals;
using namespace kohzu::controller;
using kohzu::protocol::Response;

static std::atomic<bool> g_stop{false};
static KohzuManager* g_manager_ptr = nullptr;

void sigint_handler(int /*signum*/) {
    g_stop.store(true);
    if (g_manager_ptr) {
        try {
            g_manager_ptr->stop();
        } catch (...) {}
    }
}

static std::vector<std::string> split_ws(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

static std::vector<int> parse_axis_list(const std::string& s) {
    std::vector<int> out;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, ',')) {
        try {
            int a = std::stoi(item);
            out.push_back(a);
        } catch (...) {
            // skip invalid
        }
    }
    return out;
}

int main(int argc, char** argv) {
    std::string host = "192.168.1.120";
    uint16_t port = 12321;
    bool autoReconnect = false;

    // Parse optional CLI args: host port autoreconnect
    if (argc >= 2) host = argv[1];
    if (argc >= 3) {
        try { port = static_cast<uint16_t>(std::stoi(argv[2])); } catch (...) {}
    }
    if (argc >= 4) {
        std::string ar = argv[3];
        if (ar == "1" || ar == "true" || ar == "yes") autoReconnect = true;
    }

    KohzuManager manager(host, port, autoReconnect);
    g_manager_ptr = &manager;

    // install SIGINT handler for graceful shutdown
    std::signal(SIGINT, sigint_handler);

    // register spontaneous handler: print raw line and parsed details
    manager.registerSpontaneousHandler([](const Response& resp) {
        // This handler will be invoked asynchronously (Dispatcher uses async)
        std::cout << "\n[SPONT] raw: " << resp.raw << "\n";
        std::cout << "        type=" << resp.type << " cmd=" << resp.cmd;
        if (!resp.axis.empty()) std::cout << " axis=" << resp.axis;
        if (!resp.params.empty()) {
            std::cout << " params=[";
            for (size_t i=0;i<resp.params.size();++i) {
                if (i) std::cout << ",";
                std::cout << resp.params[i];
            }
            std::cout << "]";
        }
        std::cout << std::endl << "> " << std::flush; // prompt
    });

    // start manager (either startAsync which may start recon thread, or connect once)
    if (autoReconnect) {
        std::cout << "[CLI] Starting manager with autoReconnect ON\n";
        manager.startAsync();
    } else {
        std::cout << "[CLI] Attempting single connect to " << host << ":" << port << " ...\n";
        bool ok = manager.connectOnce();
        if (!ok) {
            std::cerr << "[CLI] connectOnce failed. You can run 'start' to enable auto reconnect.\n";
        } else {
            std::cout << "[CLI] connected\n";
        }
    }

    std::cout << "kohzu-controller CLI (manager-based)\n";
    std::cout << "Type 'help' for commands.\n";

    std::string line;
    while (!g_stop.load()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            // EOF or error -> exit
            break;
        }
        auto toks = split_ws(line);
        if (toks.empty()) continue;

        const std::string& cmd = toks[0];
        if (cmd == "help") {
            std::cout << "Commands:\n"
                      << "  help                        : show this help\n"
                      << "  start                       : start manager (autoReconnect mode)\n"
                      << "  connect                     : attempt single connect (connectOnce)\n"
                      << "  move abs <axis> <pos>       : move axis to absolute position (async)\n"
                      << "  poll set <a,b,c>            : set poll axes list (comma separated)\n"
                      << "  poll add <axis>             : add a poll axis\n"
                      << "  poll rm <axis>              : remove a poll axis\n"
                      << "  state                       : print state cache snapshot\n"
                      << "  quit                        : exit CLI\n";
            continue;
        } else if (cmd == "start") {
            // enable manager reconnect loop
            try {
                manager.startAsync();
                std::cout << "[CLI] manager startAsync called\n";
            } catch (const std::exception& e) {
                std::cerr << "[CLI] startAsync error: " << e.what() << "\n";
            }
            continue;
        } else if (cmd == "connect") {
            bool ok = manager.connectOnce();
            std::cout << (ok ? "[CLI] connectOnce succeeded\n" : "[CLI] connectOnce failed\n");
            continue;
        } else if (cmd == "move") {
            if (toks.size() >= 4 && toks[1] == "abs") {
                int axis = 0;
                long pos = 0;
                try { axis = std::stoi(toks[2]); } catch (...) { std::cerr << "[CLI] invalid axis\n"; continue; }
                try { pos = std::stol(toks[3]); } catch (...) { std::cerr << "[CLI] invalid pos\n"; continue; }

                // callback prints response or error
                KohzuManager::AsyncCallback cb = [axis](const Response& r, std::exception_ptr ep) {
                    if (ep) {
                        try { std::rethrow_exception(ep); }
                        catch (const std::exception& e) {
                            std::cerr << "[MOVE cb] axis " << axis << " error: " << e.what() << "\n";
                        } catch (...) {
                            std::cerr << "[MOVE cb] axis " << axis << " unknown exception\n";
                        }
                    } else {
                        std::cout << "[MOVE cb] axis " << axis << " response raw: " << r.raw << "\n";
                    }
                };

                manager.moveAbsoluteAsync(axis, pos, cb);
                std::cout << "[CLI] moveAbsoluteAsync dispatched\n";
            } else {
                std::cerr << "[CLI] usage: move abs <axis> <pos>\n";
            }
            continue;
        } else if (cmd == "poll") {
            if (toks.size() >= 3 && toks[1] == "set") {
                // join rest tokens into one string after 'set'
                std::string rest;
                size_t pos = line.find("set");
                if (pos != std::string::npos) {
                    rest = line.substr(pos + 3);
                }
                // trim
                auto first_non = rest.find_first_not_of(" \t");
                if (first_non != std::string::npos) rest = rest.substr(first_non);
                auto axes = parse_axis_list(rest);
                manager.setPollAxes(axes);
                std::cout << "[CLI] poll axes set\n";
            } else if (toks.size() >= 3 && toks[1] == "add") {
                try {
                    int axis = std::stoi(toks[2]);
                    manager.addPollAxis(axis);
                    std::cout << "[CLI] poll add " << axis << "\n";
                } catch (...) {
                    std::cerr << "[CLI] invalid axis\n";
                }
            } else if (toks.size() >= 3 && (toks[1] == "rm" || toks[1] == "remove")) {
                try {
                    int axis = std::stoi(toks[2]);
                    manager.removePollAxis(axis);
                    std::cout << "[CLI] poll remove " << axis << "\n";
                } catch (...) {
                    std::cerr << "[CLI] invalid axis\n";
                }
            } else {
                std::cerr << "[CLI] poll commands: poll set <a,b>, poll add <axis>, poll rm <axis>\n";
            }
            continue;
        } else if (cmd == "state") {
            auto snap = manager.snapshotState();
            if (snap.empty()) {
                std::cout << "[CLI] state cache empty\n";
            } else {
                std::cout << "State snapshot (" << snap.size() << " axes):\n";
                auto now = std::chrono::steady_clock::now();
                for (auto &kv : snap) {
                    int axis = kv.first;
                    const AxisState &st = kv.second;
                    std::cout << "  axis " << axis << " : ";
                    if (st.position.has_value()) std::cout << "pos=" << *st.position << " ";
                    else std::cout << "pos=N/A ";
                    if (st.running.has_value()) std::cout << "running=" << (*st.running ? "1" : "0") << " ";
                    else std::cout << "running=N/A ";
                    if (!st.raw.empty()) std::cout << "raw=\"" << st.raw << "\" ";
                    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.lastUpdated).count();
                    std::cout << "age=" << age << "ms\n";
                }
            }
            continue;
        } else if (cmd == "quit" || cmd == "exit") {
            std::cout << "[CLI] quitting...\n";
            break;
        } else {
            std::cerr << "[CLI] unknown command: " << cmd << " (type 'help')\n";
        }
    } // main loop

    // graceful shutdown
    try {
        manager.stop();
    } catch (...) {}
    std::cout << "[CLI] exited\n";
    return 0;
}
