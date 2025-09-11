#pragma once
#include <string>
#include <vector>
#include <sstream>

namespace kohzu::protocol {

struct Response {
    bool valid{false};
    std::string command;
    int axis{-1};
    std::vector<std::string> params;
    std::string raw;
};

struct ParseResult {
    bool valid;
    Response resp;
};

struct Parser {
    // parse protocol line: optional STX (0x02), TAB separated tokens, CRLF terminated
    static ParseResult parse(const std::string& line) {
        Response r;
        r.raw = line;
        std::string s = line;
        // trim trailing CR/LF
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty() && static_cast<unsigned char>(s.front()) == 0x02) s.erase(s.begin());
        if (s.empty()) return {false, r};

        std::vector<std::string> toks;
        std::string cur;
        std::istringstream iss(s);
        while (std::getline(iss, cur, '\t')) toks.push_back(cur);
        if (toks.empty()) return {false, r};

        r.command = toks[0];
        for (size_t i = 1; i < toks.size(); ++i) r.params.push_back(toks[i]);

        if (!r.params.empty()) {
            try {
                r.axis = std::stoi(r.params[0]);
            } catch (...) {
                r.axis = -1;
            }
        } else r.axis = -1;

        r.valid = !r.command.empty();
        return {r.valid, r};
    }
};

} // namespace kohzu::protocol
