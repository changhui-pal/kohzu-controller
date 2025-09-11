#pragma once

// include/protocol/CommandBuilder.hpp

#include <string>
#include <vector>

namespace kohzu::protocol {

struct CommandBuilder {
    // Build command according to spec:
    // <CMD> or <CMD><param0> or <CMD><param0>/<param1>/...
    // params[0] is appended directly after CMD (no separator).
    // subsequent params are separated by '/'.
    // includeSTX: if true, prefix 0x02 (STX).
    static std::string makeCommand(const std::string& cmd,
                                   const std::vector<std::string>& params = std::vector<std::string>(),
                                   bool includeSTX = false) {
        std::string out;
        if (includeSTX) {
            out.push_back(static_cast<char>(0x02));
        }
        out += cmd;
        if (!params.empty()) {
            out += params[0];
            for (std::size_t i = 1; i < params.size(); ++i) {
                out.push_back('/');
                out += params[i];
            }
        }
        out += "\r\n";
        return out;
    }
};

} // namespace kohzu::protocol
