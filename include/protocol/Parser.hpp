#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

namespace kohzu::protocol {

/**
 * Response: parsed line from device
 * - valid: parsing success
 * - command: command token (e.g., "RDP", "STR")
 * - axis: axis index if applicable (-1 if none or invalid)
 * - params: tokenized parameters (strings)
 * - raw: original raw line
 */
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
    // Simple parse: split by '\t' tokens. Accepts optional leading STX (0x02) and trailing CRLF.
    static ParseResult parse(const std::string& line) {
        Response r;
        r.raw = line;
        std::string s = line;
        // trim trailing CR/LF
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        // strip leading STX if present
        if (!s.empty() && static_cast<unsigned char>(s.front()) == 0x02) {
            s.erase(s.begin());
        }
        if (s.empty()) {
            return {false, r};
        }

        // split by tab
        std::vector<std::string> toks;
        std::string cur;
        std::istringstream iss(s);
        while (std::getline(iss, cur, '\t')) {
            toks.push_back(cur);
        }
        if (toks.empty()) return {false, r};

        r.command = toks[0];
        // params: everything except command
        for (size_t i = 1; i < toks.size(); ++i) {
            r.params.push_back(toks[i]);
        }

        // try axis from first param if numeric
        if (!r.params.empty()) {
            try {
                size_t idx = 0;
                int ax = std::stoi(r.params[0], &idx);
                (void)idx;
                r.axis = ax;
            } catch (...) {
                r.axis = -1;
            }
        } else {
            r.axis = -1;
        }

        r.valid = !r.command.empty();
        return {r.valid, r};
    }
};

} // namespace kohzu::protocol
