# Kohzu Controller 라이브러리

## 개요
`kohzu-controller`는 Kohzu ARIES/LYNX 모션 컨트롤러를 TCP를 통해 제어하는 C++ 정적 라이브러리입니다. 비동기 명령 처리, 스레드 안전 상태 관리, 주기적 모니터링 기능을 제공합니다. 계층화된 아키텍처로 설계되어 통신, 프로토콜, 제어 로직을 명확히 분리했습니다. 이 라이브러리는 모션 컨트롤러의 명령(예: 이동, 원점 복귀)을 처리하며, 실시간 상태 업데이트를 지원합니다.

---

## 주요 기능
- **TCP 통신**: Boost.Asio를 활용한 비동기 읽기/쓰기.
- **프로토콜 처리**: 명령 형식화(예: APS, RPS, ORG) 및 탭 구분 응답 파싱.
- **고수준 API**: 절대/상대 이동, 원점 복귀, 시스템 설정 명령 지원.
- **스레드 안전**: mutex와 condition_variable을 사용한 안전한 상태 관리.
- **주기적 모니터링**: 축 위치와 상태(RDP/STR 명령)를 주기적으로 폴링.
- **오류 처리**: 연결, 프로토콜, 타임아웃 예외 처리.

---

## 의존성
- **Boost** (Asio 모듈): 비동기 I/O 처리.
- **spdlog**: 디버그 및 에러 로깅.

---

## 빌드 방법
1. vcpkg 또는 수동으로 의존성 설치:
   ```bash
   vcpkg install boost-asio spdlog
   ```
2. CMake 빌드 디렉토리 생성:
   ```bash
   cmake -B build -S .
   ```
3. 프로젝트 빌드:
   ```bash
   cmake --build build
   ```

---

## 사용 예시
Kohzu 컨트롤러에 연결하고 축 1을 이동시키는 예제 코드입니다:

```cpp
#include "controller/KohzuController.h"
#include <boost/asio.hpp>

int main() {
    boost::asio::io_context io;
    auto client = std::make_shared<TcpClient>(io, "192.168.1.120", "12321");
    client->connect("192.168.1.120", "12321");
    auto handler = std::make_shared<ProtocolHandler>(client);
    auto state = std::make_shared<AxisState>();
    auto controller = std::make_shared<KohzuController>(handler, state);
    
    controller->start();
    controller->startMonitoring(100); // 100ms 주기 폴링
    controller->addAxisToMonitor(1);
    controller->moveAbsolute(1, 1000, 5); // 축 1을 위치 1000으로, 속도 5로 이동
    // ...
    return 0;
}
```

---

## 프로젝트 구조
```
kohzu-controller/
├── CMakeLists.txt
├── include/
│   ├── common/ThreadSafeQueue.h
│   ├── controller/AxisState.h, KohzuController.h
│   ├── core/ICommunicationClient.h, TcpClient.h
│   └── protocol/ProtocolHandler.h, exceptions/*.h
└── src/
    ├── common/ThreadSafeQueue.cpp
    ├── controller/AxisState.cpp, KohzuController.cpp
    ├── core/TcpClient.cpp
    └── protocol/ProtocolHandler.cpp, exceptions/*.cpp
```

---

## 클래스 명세
아래는 주요 클래스의 세부 명세입니다. 각 클래스의 목적, 주요 메서드, 속성을 설명합니다.

### ICommunicationClient (인터페이스)
- **목적**: 통신 클라이언트의 추상 인터페이스. 비동기 연결/읽기/쓰기를 정의.
- **주요 메서드**:
  - `virtual void connect(const std::string& host, const std::string& port)`: 호스트와 포트로 연결.
  - `virtual void asyncWrite(const std::string& data)`: 데이터 비동기 전송.
  - `virtual void asyncRead(std::function<void(const std::string&)> callback)`: 데이터 비동기 수신 및 콜백 호출.
- **속성**: 없음 (순수 가상 클래스).

### TcpClient (클래스, ICommunicationClient 구현)
- **목적**: Boost.Asio를 사용한 TCP 클라이언트 구현. 소켓 연결과 비동기 I/O 관리.
- **주요 메서드**:
  - `TcpClient(boost::asio::io_context& ioContext, const std::string& host, const std::string& port)`: 생성자, 소켓과 리졸버 초기화.
  - `void connect(const std::string& host, const std::string& port)`: 연결 시도, 오류 시 ConnectionException 발생.
  - `void asyncRead(std::function<void(const std::string&)> callback)`: '\n'까지 비동기 읽기, 버퍼 사용.
  - `void asyncWrite(const std::string& data)`: 데이터 비동기 쓰기.
- **속성**: `boost::asio::ip::tcp::socket socket_`, `boost::asio::ip::tcp::resolver resolver_`, `boost::asio::streambuf responseBuffer_`.

### ThreadSafeQueue<T> (템플릿 클래스)
- **목적**: 스레드 안전 큐. 콜백이나 데이터 공유에 사용.
- **주요 메서드**:
  - `void push(const T& value)`: 데이터 푸시, notify_one 호출.
  - `T pop()`: 데이터 팝, 빈 경우 wait.
  - `bool tryPop(T& value, int timeoutMs)`: 타임아웃과 함께 팝 시도.
  - `bool empty()`: 큐 빈 상태 확인.
- **속성**: `std::queue<T> queue_`, `std::mutex mutex_`, `std::condition_variable conditionVariable_`.

### AxisState (클래스)
- **목적**: 축 상태(위치, 상세 상태)를 스레드 안전하게 관리.
- **주요 메서드**:
  - `void updatePosition(int axisNo, int position)`: 위치 업데이트, spdlog 로깅.
  - `void updateStatus(int axisNo, const std::vector<std::string>& params)`: 상태 파싱 및 업데이트.
  - `int getPosition(int axisNo)`: 위치 조회 (-1 if not found).
  - `AxisStatus getStatusDetails(int axisNo)`: 상태 구조체 조회.
- **속성**: `std::map<int, int> positions_`, `std::map<int, AxisStatus> statuses_`, `std::mutex mutex_`.

### ProtocolHandler (클래스)
- **목적**: 프로토콜 명령 전송과 응답 처리. 콜백 큐 관리.
- **주요 메서드**:
  - `ProtocolHandler(std::shared_ptr<ICommunicationClient> client)`: 생성자.
  - `void initialize()`: 비동기 읽기 시작.
  - `void sendCommand(const std::string& baseCommand, int axisNo, const std::vector<std::string>& params, std::function<void(const ProtocolResponse&)> callback)`: 명령 형식화 및 전송.
  - `void handleRead(const std::string& responseData)`: 응답 처리 및 콜백 호출.
  - `ProtocolResponse parseResponse(const std::string& response)`: 응답 파싱.
- **속성**: `std::shared_ptr<ICommunicationClient> client_`, `std::map<std::string, ThreadSafeQueue<...>> responseCallbacks_`, `std::mutex callbackMutex_`.

### KohzuController (클래스)
- **목적**: 고수준 제어 로직. 모니터링 스레드 관리.
- **주요 메서드**:
  - `KohzuController(std::shared_ptr<ProtocolHandler> protocolHandler, std::shared_ptr<AxisState> axisState)`: 생성자.
  - `void start()`: 프로토콜 초기화.
  - `void startMonitoring(int periodMs)`: 모니터링 스레드 시작.
  - `void stopMonitoring()`: 모니터링 중지.
  - `void addAxisToMonitor(int axisNo)`, `void removeAxisToMonitor(int axisNo)`: 모니터링 축 추가/제거.
  - `void moveAbsolute(int axisNo, int position, int speed = 0, int responseType = 0, callback)`: 절대 이동.
  - 유사하게 `moveRelative`, `moveOrigin`, `setSystem`.
- **속성**: `std::shared_ptr<ProtocolHandler> protocolHandler_`, `std::shared_ptr<AxisState> axisState_`, `std::unique_ptr<std::thread> monitoringThread_`.

### Exceptions (클래스들)
- **ConnectionException**, **ProtocolException**, **TimeoutException**: std::runtime_error 상속, 메시지 생성.

---

## 주요 코드 설명
아래는 핵심 코드 부분의 설명입니다. 코드 스니펫과 함께 동작 원리를 세부적으로 설명합니다.

### 명령 전송 (ProtocolHandler::sendCommand)
```cpp
void ProtocolHandler::sendCommand(const std::string& baseCommand, int axisNo, const std::vector<std::string>& params, std::function<void(const ProtocolResponse&)> callback) {
    std::string fullCommand = baseCommand;
    if (axisNo != -1) fullCommand += std::to_string(axisNo);
    if (!params.empty()) {
        if (axisNo != -1) fullCommand += "/";
        for (size_t i = 0; i < params.size(); ++i) {
            fullCommand += params[i];
            if (i < params.size() - 1) fullCommand += "/";
        }
    }
    fullCommand += "\r\n";
    std::lock_guard<std::mutex> lock(callbackMutex_);
    responseCallbacks_[generateResponseKey(baseCommand, axisNo)].push(callback);
    spdlog::info("Sending command: {}", fullCommand);
    client_->asyncWrite(fullCommand);
}
```
- **설명**: 명령어를 형식화하여 ("\r\n" 종료) 전송. 콜백을 키("command+axis") 기반 큐에 푸시. mutex로 스레드 안전 보장. spdlog로 로깅.

### 응답 파싱 (ProtocolHandler::parseResponse)
```cpp
ProtocolResponse ProtocolHandler::parseResponse(const std::string& response) {
    ProtocolResponse parsed;
    parsed.fullResponse = response;
    std::string cleaned = response; // \r\n 제거
    std::stringstream ss(cleaned);
    std::vector<std::string> tokens;
    std::string token;
    while (std::getline(ss, token, '\t')) tokens.push_back(token);
    if (tokens.empty()) throw ProtocolException("Empty response");
    parsed.status = tokens[0][0];
    if (tokens.size() > 1) {
        // command와 axis 파싱
    }
    // params 추가
    return parsed;
}
```
- **설명**: 응답을 탭으로 분리하여 status, command, axis, params 추출. 오류 시 예외 발생. cleanedResponse로 \r\n 처리.

### 모니터링 스레드 (KohzuController::monitorThreadFunction)
```cpp
void KohzuController::monitorThreadFunction(int periodMs) {
    while (isMonitoringRunning_.load()) {
        std::vector<int> current_axes;
        {
            std::unique_lock<std::mutex> lock(monitorMutex_);
            monitorCv_.wait(lock, [this] { return !isMonitoringRunning_.load() || !axesToMonitor_.empty(); });
            if (!isMonitoringRunning_.load()) break;
            current_axes = axesToMonitor_;
        }
        for (int axis_no : current_axes) {
            readPosition(axis_no); // RDP 명령
            readStatus(axis_no); // STR 명령
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(periodMs));
    }
}
```
- **설명**: condition_variable로 대기, 축 목록 복사 후 폴링. atomic으로 중지 제어. sleep_for 주기 대기.

---

## 아키텍처
'''Mermaid
classDiagram
    direction TB

    class ICommunicationClient {
        <<interface>>
        +connect(host: string, port: string) void
        +asyncWrite(data: string) void
        +asyncRead(callback: function) void
    }

    class TcpClient {
        -socket_: tcp::socket
        -resolver_: tcp::resolver
        -responseBuffer_: streambuf
        +connect(host: string, port: string) void
        +asyncRead(callback: function) void
        +asyncWrite(data: string) void
    }

    class ThreadSafeQueue~T~ {
        <<template>>
        -queue_: queue~T~
        -mutex_: mutex
        -conditionVariable_: condition_variable
        +push(value: T) void
        +pop() T
        +tryPop(value: T&, timeoutMs: int) bool
        +empty() bool
    }

    class AxisState {
        -positions_: map<int, int>
        -statuses_: map<int, AxisStatus>
        -mutex_: mutex
        +updatePosition(axisNo: int, position: int) void
        +updateStatus(axisNo: int, params: vector<string>) void
        +getPosition(axisNo: int) int
        +getStatusDetails(axisNo: int) AxisStatus
    }

    class ProtocolHandler {
        -client_: shared_ptr<ICommunicationClient>
        -responseCallbacks_: map<string, ThreadSafeQueue<function>>
        -callbackMutex_: mutex
        +initialize() void
        +sendCommand(baseCommand: string, axisNo: int, params: vector<string>, callback: function) void
        +handleRead(responseData: string) void
        +parseResponse(response: string) ProtocolResponse
    }

    class KohzuController {
        -protocolHandler_: shared_ptr<ProtocolHandler>
        -axisState_: shared_ptr<AxisState>
        -monitoringThread_: unique_ptr<thread>
        -axesToMonitor_: vector<int>
        -monitorMutex_: mutex
        -monitorCv_: condition_variable
        +start() void
        +startMonitoring(periodMs: int) void
        +stopMonitoring() void
        +addAxisToMonitor(axisNo: int) void
        +removeAxisToMonitor(axisNo: int) void
        +moveAbsolute(axisNo: int, position: int, speed: int, responseType: int, callback: function) void
        +moveRelative(axisNo: int, distance: int, speed: int, responseType: int, callback: function) void
        +moveOrigin(axisNo: int, speed: int, responseType: int, callback: function) void
        +setSystem(axisNo: int, systemNo: int, value: int, callback: function) void
    }

    class ProtocolResponse {
        <<struct>>
        +status: char
        +axisNo: int
        +command: string
        +params: vector<string>
        +fullResponse: string
    }

    class AxisStatus {
        <<struct>>
        +drivingState: int
        +emgSignal: int
        +orgNorgSignal: int
        +cwCcwLimitSignal: int
        +softLimitState: int
        +correctionAllowableRange: int
    }

    %% 관계 정의
    TcpClient ..|> ICommunicationClient : implements
    ProtocolHandler o--> ICommunicationClient : uses
    ProtocolHandler o--> ThreadSafeQueue : uses
    ProtocolHandler --> ProtocolResponse : produces
    KohzuController o--> ProtocolHandler : uses
    KohzuController o--> AxisState : uses
    AxisState --> AxisStatus : contains
    KohzuController --> ThreadSafeQueue : monitors with
'''
- **코어 계층**: `TcpClient`가 Boost.Asio로 TCP 통신 관리.
- **프로토콜 계층**: `ProtocolHandler`가 명령 형식화 및 응답 파싱.
- **컨트롤러 계층**: `KohzuController`가 이동/모니터링 API 제공.
- **공통 유틸리티**: `ThreadSafeQueue`로 콜백 관리.
- **스레드 안전성**: `AxisState`와 `ProtocolHandler`에서 mutex로 데이터 보호.
- **모니터링**: 별도 스레드에서 주기적으로 위치/상태 업데이트.

---

## 확장 가능성
- 다축 동기화 명령 추가.
- `ICommunicationClient`를 활용한 UDP/시리얼 통신 지원.
- 새로운 Kohzu 명령어 추가 가능.

---

## 라이선스

