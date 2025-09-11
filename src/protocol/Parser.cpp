// src/protocol/Parser.cpp
#include "protocol/Parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace kohzu::protocol {

namespace {
    // split s by delimiter into vector<string>
    static std::vector<std::string> split_by_tab(const std::string& s) {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start < s.size()) {
            auto pos = s.find('\t', start);
            if (pos == std::string::npos) {
                out.push_back(s.substr(start));
                break;
            } else {
                out.push_back(s.substr(start, pos - start));
                start = pos + 1;
            }
        }
        // handle empty input -> empty vector
        return out;
    }

    static bool is_digits(const std::string& s) {
        if (s.empty()) return false;
        return std::all_of(s.begin(), s.end(), [](unsigned char c){ return std::isdigit(c); });
    }

    static std::string to_upper3(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size() && i < 3; ++i) out.push_back(static_cast<char>(std::toupper((unsigned char)s[i])));
        return out;
    }
} // namespace

Response Parser::parse(const std::string& lineIn) {
    Response resp;
    resp.raw = lineIn;

    if (lineIn.empty()) {
        resp.valid = false;
        return resp;
    }

    // If the line starts with STX (0x02), skip it
    std::string line = lineIn;
    if (!line.empty() && static_cast<unsigned char>(line[0]) == 0x02) {
        if (line.size() == 1) {
            resp.valid = false;
            return resp;
        }
        line = line.substr(1);
    }

    // First char must be C/W/E
    char t = line[0];
    if (t != 'C' && t != 'W' && t != 'E') {
        resp.valid = false;
        return resp;
    }
    resp.type = t;

    // Payload is after first char; typically starts with a tab, but we support both:
    std::string payload;
    if (line.size() >= 2 && line[1] == '\t') {
        payload = line.substr(2);
    } else if (line.size() >= 2) {
        payload = line.substr(1);
    } else {
        // no payload
        resp.valid = false;
        return resp;
    }

    // Split payload by tab
    auto fields = split_by_tab(payload);
    if (fields.empty()) {
        resp.valid = false;
        return resp;
    }

    // First field contains cmd (first 3 chars) and optional axis as trailing digits
    std::string cmdField = fields[0];
    if (cmdField.size() < 3) {
        resp.valid = false;
        return resp;
    }

    resp.cmd = to_upper3(cmdField);

    // axis: if extra chars after first 3, they must be digits; otherwise axis empty
    if (cmdField.size() > 3) {
        std::string axisPart = cmdField.substr(3);
        if (is_digits(axisPart)) {
            resp.axis = axisPart;
        } else {
            // For commands like SYS or others that may contain non-digit tails,
            // if cmd==SYS we allow params and do not require numeric axis.
            if (resp.cmd == "SYS") {
                resp.axis.clear();
                // For SYS, treat the whole cmdField remainder as first param instead
                resp.params.reserve(fields.size());
                // push the remainder as first param
                resp.params.push_back(axisPart);
                // copy rest fields (fields[1..])
                for (size_t i = 1; i < fields.size(); ++i) resp.params.push_back(fields[i]);
                resp.valid = true;
                return resp;
            } else {
                resp.valid = false;
                return resp;
            }
        }
    }

    // remaining fields are params
    if (fields.size() > 1) {
        resp.params.reserve(fields.size() - 1);
        for (size_t i = 1; i < fields.size(); ++i) resp.params.push_back(fields[i]);
    }

    resp.valid = true;
    return resp;
}

} // namespace kohzu::protocol
