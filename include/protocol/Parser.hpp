#pragma once

// include/protocol/Parser.hpp
// Strict parser:
// - first char must be 'C'|'W'|'E'
// - then '\t' and cmdField (must be at least 3 chars)
// - cmd = first 3 chars (uppercased)
// - if cmdField length > 3 then tail must be ALL DIGITS to be axis; else parse fails (invalid)
// - SYS special-case: axis empty; only first param (error/warn number) is kept
// - If protocol violation -> Response.valid == false (caller should ignore)

#include <string>
#include <vector>
#include <cctype>

namespace kohzu::protocol {

struct Response {
    char type = 0;                    // 'C'|'W'|'E'
    std::string cmd;                  // exactly 3-letter command uppercase (e.g. "MPS","STR","SYS")
    std::string axis;                 // axis digits (empty if none)
    std::vector<std::string> params;  // remaining tab-separated fields (for SYS at most 1 item)
    std::string raw;                  // original raw line (CRLF stripped)

    bool valid = false;               // true if parse matched protocol
};

// split by delim and keep empty tokens
static std::vector<std::string> split_keep_empty(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string token;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == delim) {
            out.push_back(token);
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    out.push_back(token);
    return out;
}

static bool is_all_digits(const std::string& str) {
    if (str.empty()) return false;
    for (std::size_t i = 0; i < str.size(); ++i) {
        unsigned char uc = static_cast<unsigned char>(str[i]);
        if (!std::isdigit(uc)) return false;
    }
    return true;
}

static void upper_n(std::string& dst, const std::string& src, std::size_t n) {
    dst.clear();
    std::size_t limit = (src.size() < n) ? src.size() : n;
    for (std::size_t i = 0; i < limit; ++i) {
        unsigned char uc = static_cast<unsigned char>(src[i]);
        dst.push_back(static_cast<char>(std::toupper(uc)));
    }
}

class Parser {
public:
    // Parse a single CRLF-stripped line. If invalid, returns Response with valid=false.
    static Response parse(const std::string& line) {
        Response resp;
        resp.raw = line;

        if (line.empty()) {
            resp.valid = false;
            return resp;
        }

        // first char check
        char first = line[0];
        if (!(first == 'C' || first == 'W' || first == 'E')) {
            resp.valid = false;
            return resp;
        }
        resp.type = first;

        // extract rest after optional tab
        std::string rest;
        if (line.size() > 1 && line[1] == '\t') {
            rest = line.substr(2);
        } else if (line.size() > 1) {
            rest = line.substr(1);
            if (!rest.empty() && rest.front() == '\t') {
                rest = rest.substr(1);
            }
        } else {
            resp.valid = false;
            return resp;
        }

        std::vector<std::string> fields = split_keep_empty(rest, '\t');
        if (fields.empty()) {
            resp.valid = false;
            return resp;
        }

        std::string cmdField = fields[0];
        if (cmdField.size() < 3) {
            resp.valid = false;
            return resp;
        }

        // cmd is always first 3 chars (uppercased)
        upper_n(resp.cmd, cmdField, 3);

        // if tail exists, it must be all digits to be axis
        if (cmdField.size() > 3) {
            std::string tail = cmdField.substr(3);
            if (is_all_digits(tail)) {
                resp.axis = tail;
            } else {
                resp.valid = false;
                return resp;
            }
        } else {
            resp.axis.clear();
        }

        // SYS special handling
        if (resp.cmd == "SYS") {
            resp.axis.clear();
            resp.params.clear();
            if (fields.size() >= 2 && !fields[1].empty()) {
                resp.params.push_back(fields[1]); // only first param (error/warn)
            }
            resp.valid = true;
            return resp;
        }

        // Non-SYS: params are fields[1..] (can be zero or many)
        resp.params.clear();
        if (fields.size() >= 2) {
            for (std::size_t i = 1; i < fields.size(); ++i) {
                resp.params.push_back(fields[i]);
            }
        }

        resp.valid = true;
        return resp;
    }
};

} // namespace kohzu::protocol

