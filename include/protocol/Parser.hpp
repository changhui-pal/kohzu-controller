#pragma once
/**
 * Parser.hpp
 *
 * 수신 라인(ASCII text line)을 파싱하여 Response 구조체로 반환.
 *
 * 규칙(구현에 맞춤):
 *  - 라인의 첫 문자는 'C' (normal) / 'W' (warning) / 'E' (error) 이어야 valid.
 *  - 이후 필드들은 탭('\t')으로 구분된다. 첫 필드(cmdField)의 앞 3문자가 명령(cmd).
 *    cmdField에 추가 문자가 있으면 숫자여야 하며 axis로 해석된다.
 *  - SYS 등 특수 명령은 별도 처리(필요시 params에 원문 보관).
 *  - CR/LF는 이미 제거된 라인을 입력으로 가정.
 *
 * 반환(Response):
 *  - type: 'C'/'W'/'E'
 *  - cmd: 대문자 3문자
 *  - axis: 문자열(없을수도 있음)
 *  - params: 나머지 파라미터들
 *  - raw: 원본 라인
 *  - valid: 파싱 성공 여부
 *
 * 주의: 파서가 valid=false를 반환하면 호출자가 로깅/무시/알람 등 적절히 처리해야 함.
 */

#include <string>
#include <vector>

namespace kohzu::protocol {

struct Response {
    char type{' '};                // 'C', 'W', 'E'
    std::string cmd;               // 3-letter command, e.g., "RDP", "STR", "APS"
    std::string axis;              // axis 정보(있으면)
    std::vector<std::string> params; // 기타 파라미터
    std::string raw;               // 원본 라인
    bool valid{false};             // 파싱 성공 여부
};

class Parser {
public:
    // parse: 주어진 한 라인을 파싱하여 Response 반환
    // 입력: line (CRLF 제거된 상태 권장)
    static Response parse(const std::string& line);
};

} // namespace kohzu::protocol
