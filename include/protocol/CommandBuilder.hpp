#pragma once
/**
 * CommandBuilder.hpp
 *
 * Kohzu 장비에 보낼 "한 줄" 명령 생성기.
 *
 * 포맷 (권장 기본):
 *   [optional STX(0x02)] CMD \t param1/param2/... \r\n
 *
 * - includeSTX: true면 맨 앞에 0x02(STX) 추가
 * - params: 빈 벡터이면 "CMD\r\n" 형태로 전송
 * - 내부적으로 CR/LF 제거(파라미터에 CR/LF 포함 방지)
 *
 * 주의: 장비 프로토콜이 다른 형식을 요구하면 delimiter나 필드 구성을 조정하세요.
 */

#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

namespace kohzu::protocol {

class CommandBuilder {
public:
    // join params with '/' and produce a line that ends with \r\n
    static std::string makeCommand(const std::string& cmd,
                                   const std::vector<std::string>& params,
                                   bool includeSTX = false) {
        std::string out;
        if (includeSTX) out.push_back(static_cast<char>(0x02));

        // append command (no trailing whitespace)
        out += cmd;

        // If there are params, append a tab and join with '/'
        if (!params.empty()) {
            out.push_back('\t');
            bool first = true;
            for (const auto& p : params) {
                if (!first) out.push_back('/');
                first = false;
                // sanitize param: remove CR/LF
                std::string s = p;
                s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
                s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
                out += s;
            }
        }

        // append CRLF
        out += "\r\n";
        return out;
    }
};

} // namespace kohzu::protocol
