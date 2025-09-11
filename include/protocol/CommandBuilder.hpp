#pragma once
#include <string>
#include <vector>

namespace kohzu::protocol {

struct CommandBuilder {
    // Build command line using TAB-separated params and ending CRLF.
    // includeSTX: if true, prepend 0x02 character
    static std::string makeCommand(const std::string& cmd,
                                   const std::vector<std::string>& params = {},
                                   bool includeSTX = false) {
        std::string out;
        if (includeSTX) out.push_back(static_cast<char>(0x02));
        out += cmd;
        for (const auto &p : params) {
            out.push_back('\t');
            out += p;
        }
        out += "\r\n";
        return out;
    }
};

} // namespace kohzu::protocol
