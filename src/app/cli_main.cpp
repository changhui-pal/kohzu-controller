// src/app/cli_main.cpp
#include "controller/KohzuManager.hpp"

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <unordered_map>
#include <limits>

using namespace kohzu::controller;
using namespace std::chrono_literals;

static std::vector<int> parseAxes(const std::string& s) {
    std::vector<int> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        // trim
        tok.erase(tok.begin(), std::find_if(tok.begin(), tok.end(), [](int ch){ return !std::isspace(ch); }));
        tok.erase(std::find_if(tok.rbegin(), tok.rend(), [](int ch){ return !std::isspace(ch); }).base(), tok.end());
        if (tok.empty()) continue;
        try {
            int a = std::stoi(tok);
            if (a > 0) out.push_back(a);
        } catch (...) {}
    }
    return out;
}

int main(int argc, char** argv) {
    std::string host = "192.168.1.120";
    uint16_t port = 12321;

    if (argc > 1) host = argv[1];
    if (argc > 2) {
        try { port = static_cast<uint16_t>(std::stoi(argv[2])); } catch(...) {}
    }

    std::cout << "Kohzu CLI (manager-based)\n";
    std::cout << "Host: " << host << " Port: " << port << "\n";
    std::cout << "Enter axes to monitor (comma separated), e.g. 1,2,3 : ";
    std::string axesLine;
    std::getline(std::cin, axesLine);
    auto axes = parseAxes(axesLine);
    if (axes.empty()) {
        std::cout << "No axes specified; default to axis 1\n";
        axes = {1};
    }

    // Create manager: no auto-reconnect for CLI by default; poll interval 100ms
    KohzuManager mgr(host, port, /*autoReconnect=*/false, std::chrono::milliseconds(5000), std::chrono::milliseconds(100));
    mgr.setPollAxes(axes);

    // register connection handler
    std::atomic<bool> connected{false};
    mgr.registerConnectionHandler([&connected](bool ok, const std::string& msg) {
        if (ok) {
            std::cout << "[Manager] Connected: " << msg << "\n";
            connected.store(true);
        } else {
            std::cout << "[Manager] Disconnected/Failed: " << msg << "\n";
            connected.store(false);
        }
    });

    // start connect in background
    mgr.startAsync();

    // wait up to 10s for connection
    for (int i = 0; i < 100 && !connected.load(); ++i) {
        std::this_thread::sleep_for(100ms);
    }
    if (!connected.load()) {
        std::cout << "Warning: not connected after waiting. You can still try commands (they will fail) or quit.\n";
    }

    // Monitor thread: print only when position changed, and print final position once when motion stops.
    std::atomic<bool> monitorRun{true};

    // per-axis tracking
    std::unordered_map<int, std::int64_t> lastPrinted; // last printed position
    std::unordered_map<int, bool> finalPrinted;        // whether final already printed
    std::unordered_map<int, std::chrono::steady_clock::time_point> lastChangeTime;

    const auto stableThreshold = std::chrono::milliseconds(500);

    std::thread monitorThread([&mgr, &axes, &monitorRun, &lastPrinted, &finalPrinted, &lastChangeTime, stableThreshold]() {
        while (monitorRun.load()) {
            StateCache* cache = mgr.getStateCache();
            if (cache) {
                auto snap = cache->snapshot();
                auto now = std::chrono::steady_clock::now();

                for (int a : axes) {
                    auto it = snap.find(a);
                    if (it == snap.end()) {
                        // no data yet for this axis
                        continue;
                    }
                    const auto &s = it->second;
                    std::int64_t pos = s.position;
                    bool running = s.running;

                    // if axis is running, reset finalPrinted so we will print updates
                    if (running) {
                        finalPrinted[a] = false;
                    }

                    // initialize lastPrinted sentinel if not present
                    if (lastPrinted.find(a) == lastPrinted.end()) {
                        // use a sentinel value different from likely positions
                        lastPrinted[a] = std::numeric_limits<std::int64_t>::min();
                    }

                    bool printedThisCycle = false;

                    if (pos != lastPrinted[a]) {
                        // position changed since last printed -> print update
                        // timestamp
                        auto tnow = std::chrono::system_clock::now();
                        std::time_t t = std::chrono::system_clock::to_time_t(tnow);
                        std::tm tm;
                        #ifdef _WIN32
                            localtime_s(&tm, &t);
                        #else
                            localtime_r(&t, &tm);
                        #endif
                        char buf[64];
                        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);

                        std::ostringstream oss;
                        oss << "[" << buf << "] ";
                        oss << "A" << a << ": pos=" << pos << (running ? "(run)" : "(stopped)") ;
                        std::cout << oss.str() << "\n";

                        lastPrinted[a] = pos;
                        lastChangeTime[a] = now;
                        printedThisCycle = true;
                    }

                    // If not running (motion ended), and we haven't printed final yet, check stability
                    if (!running && !finalPrinted[a]) {
                        // If we never printed (lastPrinted == sentinel), still print final once
                        if (lastPrinted[a] == std::numeric_limits<std::int64_t>::min()) {
                            // print final
                            auto tnow = std::chrono::system_clock::now();
                            std::time_t t = std::chrono::system_clock::to_time_t(tnow);
                            std::tm tm;
                            #ifdef _WIN32
                                localtime_s(&tm, &t);
                            #else
                                localtime_r(&t, &tm);
                            #endif
                            char buf[64];
                            std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);

                            std::ostringstream oss;
                            oss << "[" << buf << "] ";
                            oss << "A" << a << ": pos=" << pos << "(final)";
                            std::cout << oss.str() << "\n";

                            lastPrinted[a] = pos;
                            finalPrinted[a] = true;
                        } else {
                            // we have previous printed pos; check stability time
                            auto itTime = lastChangeTime.find(a);
                            bool stableEnough = false;
                            if (itTime != lastChangeTime.end()) {
                                stableEnough = (now - itTime->second >= stableThreshold);
                            } else {
                                // no change time recorded -- consider it stable
                                stableEnough = true;
                            }

                            if (stableEnough) {
                                // print final (even if equal to last printed)
                                auto tnow = std::chrono::system_clock::now();
                                std::time_t t = std::chrono::system_clock::to_time_t(tnow);
                                std::tm tm;
                                #ifdef _WIN32
                                    localtime_s(&tm, &t);
                                #else
                                    localtime_r(&t, &tm);
                                #endif
                                char buf[64];
                                std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);

                                std::ostringstream oss;
                                oss << "[" << buf << "] ";
                                oss << "A" << a << ": pos=" << pos << "(final)";
                                std::cout << oss.str() << "\n";

                                finalPrinted[a] = true;
                            }
                        }
                    }
                }
            } else {
                // no poller/statecache available
                // do not spam: print once per loop with small message
                // (kept as before but less verbose)
                std::cout << "[StateCache] not available (poller not created/connected)\n";
            }
            // refresh every 100 ms
            std::this_thread::sleep_for(100ms);
        }
    });

    // interactive loop
    std::cout << "Commands:\n";
    std::cout << "  abs    -> absolute move\n";
    std::cout << "  rel    -> relative move\n";
    std::cout << "  state  -> print snapshot now\n";
    std::cout << "  quit   -> exit\n";

    std::string cmd;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, cmd)) break;
        if (cmd.empty()) continue;

        if (cmd == "quit") break;
        if (cmd == "state") {
            StateCache* cache = mgr.getStateCache();
            if (!cache) {
                std::cout << "StateCache not available\n";
                continue;
            }
            auto snap = cache->snapshot();
            for (auto &p : snap) {
                const auto &s = p.second;
                std::cout << "Axis " << p.first << " pos=" << s.position << " running=" << (s.running ? "Y":"N")
                          << " updated(ms ago)="
                          << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - s.updated).count()
                          << " raw=\"" << s.lastRaw << "\"\n";
            }
            continue;
        }

        if (cmd == "abs" || cmd == "rel") {
            std::string axisStr, posStr, speedStr, respStr;
            std::cout << "axis: ";
            if (!std::getline(std::cin, axisStr)) break;
            std::cout << "position (integer): ";
            if (!std::getline(std::cin, posStr)) break;
            std::cout << "speed table (0-9) [0]: ";
            if (!std::getline(std::cin, speedStr)) speedStr = "";
            std::cout << "responseMethod (0=when completed,1=quick) [0]: ";
            if (!std::getline(std::cin, respStr)) respStr = "";

            int axis = 0;
            long long pos = 0;
            int speed = 0;
            int respMethod = 0;
            try {
                axis = std::stoi(axisStr);
                pos = std::stoll(posStr);
                if (!speedStr.empty()) speed = std::stoi(speedStr);
                if (!respStr.empty()) respMethod = std::stoi(respStr);
            } catch (...) {
                std::cout << "Invalid numeric input\n";
                continue;
            }

            // Callback prints completion and any response raw
            auto cb = [axis](const kohzu::protocol::Response& resp, std::exception_ptr ep) {
                if (ep) {
                    try { std::rethrow_exception(ep); }
                    catch (const std::exception& e) {
                        std::cout << "[CB] Axis " << axis << " error: " << e.what() << "\n";
                    } catch (...) {
                        std::cout << "[CB] Axis " << axis << " unknown error\n";
                    }
                } else {
                    std::cout << "[CB] Axis " << axis << " response raw=\"" << resp.raw << "\"\n";
                }
            };

            bool ok = false;
            if (cmd == "abs") {
                ok = mgr.moveAbsoluteAsync(axis, pos, speed, respMethod, cb);
            } else {
                ok = mgr.moveRelativeAsync(axis, pos, speed, respMethod, cb);
            }
            if (!ok) {
                std::cout << "Failed to send command (probably not connected)\n";
            } else {
                // clear finalPrinted for that axis so monitor will print updates for new op
                finalPrinted[axis] = false;
                lastPrinted[axis] = std::numeric_limits<std::int64_t>::min();
                lastChangeTime.erase(axis);
                std::cout << "Command sent (axis=" << axis << ")\n";
            }
            continue;
        }

        std::cout << "Unknown command\n";
    }

    // shutdown
    monitorRun.store(false);
    if (monitorThread.joinable()) monitorThread.join();

    mgr.stop();

    std::cout << "Exited\n";
    return 0;
}

