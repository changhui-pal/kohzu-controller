kohzu-controller

**Kohzu ARIES/LYNX 계열 모터 컨트롤러(문자열 기반 TCP)**를 제어하기 위한 C++ 라이브러리 및 예제 애플리케이션용 README입니다.
이 문서는 프로젝트의 개요, 요구사항, 빌드 방법, 저장소 구성, 주요 구성요소(클래스) 설명, 및 전체 동작 흐름을 실제 소스 파일에 존재하는 선언/구현에 기반하여 정리한 내용입니다.

목차

개요

요구사항

빌드 방법

프로젝트 구성 (파일/디렉토리)

주요 구성요소(클래스/모듈) 설명

전체 동작 방식(요청→응답 시퀀스)

사용 시 주의사항 / 운영 팁

개요

kohzu-controller는 Kohzu 장비와의 TCP 기반 통신을 담당하는 C++ 프로젝트로, 다음을 제공합니다.

Boost.Asio 기반 TCP 클라이언트 구현(AsioTcpClient)

송신 전용 큐/스레드 기반 안전한 전송기(Writer)

장비 명령 문자열 생성기(CommandBuilder)

수신 라인 파서(Parser) — C/W/E 접두사 규칙 반영

요청-응답 매칭을 담당하는 디스패처(Dispatcher)

상위 제어 레이어: MotorController (동기/비동기 API) 및 KohzuManager (연결 관리·폴링 오케스트레이션)

필요 시 폴링으로 상태 캐시를 갱신하는 Poller 및 StateCache

샘플 CLI(cli_main.cpp) — 대화형 테스트 유틸리티

(위 항목은 저장소의 헤더·소스 파일에 실제로 선언/구현된 내용에 근거합니다.)

요구사항

C++17 이상을 지원하는 컴파일러 (gcc / clang 등)

CMake 3.10 이상

Boost (특히 Boost::system, Boost::asio) — CMake에서 find_package(Boost REQUIRED COMPONENTS system) 호출

pthread (Linux 환경에서 스레드 사용)

프로젝트의 CMake 설정은 CMakeLists.txt에 정의되어 있으며, 실행파일 타깃으로 kohzu_controller를 생성하도록 되어 있습니다.

빌드 방법
git clone https://github.com/changhui-pal/kohzu-controller.git
cd kohzu-controller
mkdir -p build && cd build
cmake ..
make -j
# 빌드 후 실행 파일(예: kohzu_controller)이 생성됩니다.


만약 Boost를 찾지 못하면 OS 패키지 매니저로 Boost 라이브러리를 설치하세요 (예: Ubuntu의 경우 libboost-system-dev 등).

프로젝트 구성 (루트 기준)
CMakeLists.txt
include/
  comm/
    ITcpClient.hpp
    AsioTcpClient.hpp
    Writer.hpp
  config/
    Config.hpp
  protocol/
    CommandBuilder.hpp
    Parser.hpp
    Dispatcher.hpp
  controller/
    StateCache.hpp
    Poller.hpp
    MotorController.hpp
    KohzuManager.hpp
src/
  comm/
    AsioTcpClient.cpp
    Writer.cpp
  protocol/
    Dispatcher.cpp
  controller/
    Poller.cpp
    MotorController.cpp
    KohzuManager.cpp
  app/
    cli_main.cpp


각 파일/모듈의 목적은 다음 섹션에서 상세히 설명합니다.

주요 구성요소(클래스/모듈) — 파일 기반 설명

아래 설명은 각 헤더·소스에 실제로 존재하는 선언·구현을 근거로 작성되었습니다.

comm::ITcpClient (include/comm/ITcpClient.hpp)

종류: 추상 인터페이스

핵심 기능(선언된 메서드):

void connect(const std::string& host, uint16_t port) — 연결 시도 (구현체가 예외를 던질 수 있음)

void close() — 연결 종료

void registerRecvHandler(RecvHandler cb) — CRLF 단위 라인 수신 콜백 등록

boost::asio::ip::tcp::socket& socket() — 내부 소켓 접근자 (Writer 생성 시 사용)

comm::AsioTcpClient (include/comm/AsioTcpClient.hpp, src/comm/AsioTcpClient.cpp)

종류: ITcpClient의 Boost.Asio 구현

주요 동작:

내부에 boost::asio::io_context, tcp::socket, streambuf를 보유하고, connect 호출 시 boost::asio::connect로 서버 연결 후 비동기 async_read_until("\r\n") 방식으로 라인 단위 수신을 수행합니다.

io_context는 백그라운드 스레드에서 run() 하도록 구성됩니다.

수신 라인을 읽은 뒤 등록된 recv handler를 호출합니다. 읽기 오류 발생 시 로그 처리됩니다.

comm::Writer (include/comm/Writer.hpp, src/comm/Writer.cpp)

종류: 송신 전용 큐 + 전용 워커 스레드

주요 API:

Writer(boost::asio::ip::tcp::socket& socket, std::size_t maxQueueSize = 1000) — 생성 시 내부 워커 스레드 시작

void enqueue(const std::string& line) — 큐에 라인(문자열)을 넣고 워커가 순차 전송; 큐가 가득하면 std::runtime_error를 던짐

void stop() — 정리 요청 및 워커 종료

std::size_t queuedSize() const — 큐 크기 반환

특징/주의:

워커는 boost::asio::write로 블로킹 전송을 수행합니다.

큐 초과 시 예외가 발생하므로 호출 쪽에서 예외 처리가 필요합니다.

구현 주석에 GUI 스레드에서 직접 enqueue하지 말라고 권고되어 있음(블로킹/예외 가능성 때문에).

config::DEFAULT_RESPONSE_TIMEOUT_MS (include/config/Config.hpp)

값: std::chrono::milliseconds(60000) (기본 응답 타임아웃 60초)

protocol::CommandBuilder (include/protocol/CommandBuilder.hpp)

기능: 명령 문자열 생성기

핵심 함수: static std::string makeCommand(const std::string& cmd, const std::vector<std::string>& params, bool includeSTX = false)

includeSTX가 true면 STX(0x02)를 앞에 붙이고, 파라미터는 /로 구분, 끝에 \r\n을 붙여 반환.

protocol::Parser (include/protocol/Parser.hpp)

기능: 수신 라인(문자열) → Response 구조체로 파싱

파싱 규칙(구현 그대로):

라인의 첫 문자는 반드시 C, W, E 중 하나여야 함. 그 이외는 valid=false.

이후 필드는 탭(\t)으로 구분. 첫 필드(명령 필드)의 앞 3문자가 명령(cmd)이고, 그 뒤에 추가 문자가 있으면 숫자이어야 하며 이를 axis로 해석.

SYS 명령은 특수 취급.

출력 구조: Response { char type; std::string cmd; std::string axis; std::vector<std::string> params; std::string raw; bool valid; }

protocol::Dispatcher (include/protocol/Dispatcher.hpp, src/protocol/Dispatcher.cpp)

기능: 요청-응답 매칭과 pending 관리

주요 기능/메서드:

std::future<Response> addPending(const std::string& key) — 키로 promise/future 등록

bool tryFulfill(const std::string& key, const Response& response) — 키에 해당하는 pending이 있으면 promise.set_value(response)로 응답 전달

removePendingWithException(...) / cancelAllPendingWithException(...) — 타임아웃/종료 시 pending에 예외 설정

spontaneous 메시지(자발 전송)를 위한 등록/알림 기능 (registerSpontaneousHandler, notifySpontaneous)

특징: 키-기반 매칭(일반적으로 cmd:axis 형태로 매칭 사용)

controller::StateCache (include/controller/StateCache.hpp)

기능: 축(axis)별 상태(위치, running 플래그, 마지막 raw 응답 등)를 스레드 안전하게 보관

주요 메서드: updatePosition, updateRunning, update, get, snapshot, clear 등(모두 mutex로 보호)

controller::Poller (include/controller/Poller.hpp, src/controller/Poller.cpp)

기능: 지정된 축들을 주기적으로 폴링(RDP/STR 등)하여 StateCache를 갱신

주요 사항:

생성자에서 MotorController 공유 포인터와 축 리스트, interval을 받음(기본 100ms 등).

start() / stop()으로 백그라운드 스레드를 관리. run() 루프는 axes를 순회하며 inflight(이미 요청 중인지) 체크 후 비동기 요청을 보냄.

Poller는 Manager의 제어 하에 있으며, 모터 동작이 있을 때만 활성화되도록 관리됩니다(불필요한 폴링 회피 목적).

controller::MotorController (include/controller/MotorController.hpp, src/controller/MotorController.cpp)

기능: 상위 제어 API — 명령 생성·전송, 수신 파싱·디스패치, 동기/비동기 인터페이스 제공

주요 시그니처(파일에 있는 선언/사용 기반):

MotorController(std::shared_ptr<ITcpClient> comm) — 생성자(내부에서 recv handler 등록 및 callback worker 시작)

void connect(const std::string& host, uint16_t port) — 내부적으로 comm_->connect(host,port) 호출 후 writer 초기화

void stop() — dispatcher_.cancelAllPendingWithException(...) 호출, writer 중지, comm close 등 정리

std::future<Response> sendAsync(const std::string& cmd, const std::vector<std::string>& params) — Future 기반 비동기 전송

void sendAsync(const std::string& cmd, const std::vector<std::string>& params, AsyncCallback cb) — 콜백 기반 비동기 전송 (콜백은 내부 워커 스레드에서 호출)

Response sendSync(const std::string& cmd, const std::vector<std::string>& params, std::chrono::milliseconds timeout = DEFAULT_RESPONSE_TIMEOUT_MS) — 동기 전송 (타임아웃 시 예외)

핵심 동작:

sendXxx 호출 시 Dispatcher::addPending(key)로 pending 등록 → CommandBuilder::makeCommand로 라인 생성 → Writer::enqueue로 전송 → 응답 수신 시 onLineReceived에서 Parser::parse 후 Dispatcher::tryFulfill로 pending 완성.

콜백 스타일 비동기는 내부 callbackQueue_에 future+cb를 넣어 내부 워커가 future 준비를 기다려 cb를 호출합니다. 콜백은 내부 워커 스레드에서 실행되므로 호출자는 필요 시 GUI 스레드로 포워딩해야 합니다.

connect를 호출해 writer_가 초기화되지 않으면 sendSync/sendAsync는 예외를 던집니다(코드에 명시된 체크 사항).

controller::KohzuManager (include/controller/KohzuManager.hpp, src/controller/KohzuManager.cpp)

기능: 전체 오케스트레이션 — 연결/재연결 루프, Poller/StateCache 관리, 다축 제어의 수명 주기 조절

생성자(파일 기반):

KohzuManager(const std::string& host, uint16_t port, bool autoReconnect = false, std::chrono::milliseconds reconnectInterval = 5000ms, std::chrono::milliseconds pollInterval = 100ms)

주요 동작/메서드:

startAsync() — 백그라운드에서 연결 시도 루프를 시작(연결 성공 시 MotorController, Poller 생성)

stop() — 모든 리소스 정리(연결 스레드 조인, poller stop, motorCtrl stop, tcp client reset 등)

Poller 관리는 notifyOperationStarted() / notifyOperationFinished()로 active operation 카운터를 관리하여 동작이 진행 중일 때만 poller를 활성화하도록 함(파일 구현에 따른 행동).

이동 API: moveAbsoluteAsync, moveRelativeAsync 등이 구현되어 있으며, 내부적으로 motorCtrl->sendAsync(...)를 호출하고, 사용자 콜백 실행 후 dispatchFinalReads(RDP + STR)로 StateCache를 최신화하고 notifyOperationFinished()를 호출합니다.

샘플 앱: src/app/cli_main.cpp

기능: 빌드 후 실행 가능한 간단 CLI 예제.

특징: 기본 호스트 192.168.1.120, 포트 12321을 예시로 사용하고 대화형으로 axis/명령 입력을 받아 MotorController/KohzuManager를 통해 명령을 전송 및 상태 모니터링.

전체 동작 방식(요청 → 응답 시퀀스)

연결 준비

사용자(또는 KohzuManager)가 AsioTcpClient를 생성하고 MotorController::connect(host, port)를 호출하면 AsioTcpClient::connect가 TCP 연결을 수립하고 비동기 읽기 루프를 백그라운드 스레드에서 실행합니다. Writer는 socket()을 통해 생성됩니다.

명령 전송 (동기/비동기)

호출자가 MotorController::sendSync(...) 또는 sendAsync(...)를 호출합니다.

MotorController는 Dispatcher::addPending(key)로 pending을 등록하고, CommandBuilder::makeCommand(...)로 명령 문자열을 생성하여 Writer::enqueue(...)로 전송합니다.

장비 응답 수신

AsioTcpClient가 라인(CRLF 종료)을 읽어 MotorController::onLineReceived에 전달합니다.

Parser::parse(line)가 Response로 변환하고 Dispatcher::tryFulfill(key, response)를 호출해 pending을 완성합니다.

매칭되는 pending이 없으면 dispatcher_.notifySpontaneous(response)로 자발(Spontaneous) 메시지 처리 루틴을 호출합니다.

후처리 (Manager 관점)

KohzuManager의 이동 API는 명령 완료 콜백에서 dispatchFinalReads(RDP + STR)를 트리거하여 최종 위치/상태를 StateCache에 기록합니다.

Poller는 동작 중일 때 활성화되어 주기적으로 RDP/STR을 요청해 StateCache를 갱신하며, 동작이 없을 때는 비활성화됩니다.

사용 시 주의사항 / 운영 팁 (파일 기반 권고)

Writer 큐 한계: Writer::enqueue는 내부 큐가 가득하면 std::runtime_error 예외를 던집니다. 호출부(특히 자동 반복 호출 루틴)는 예외 처리를 반드시 구현하세요.

초기화 순서: MotorController::connect로 Writer가 초기화되지 않은 상태에서 sendSync/sendAsync를 호출하면 예외가 발생합니다. 반드시 먼저 연결을 수행하세요.

콜백 스레드: MotorController의 비동기 콜백은 내부 워커 스레드에서 실행됩니다. GUI 앱 등 메인 스레드 접근이 필요한 경우, 콜백 내부에서 메인 스레드로 포워딩하는 로직을 추가하세요.

파서 규칙 엄격성: Parser는 첫 문자 검사(C/W/E), 탭 구분, cmdField 규칙(앞 3문자 명령, 뒤 숫자는 axis) 등 엄격한 검사를 수행합니다. 장비 응답이 규격과 다르면 valid=false가 반환되고 상위에서 로깅됩니다.

타임아웃 관리: 기본 응답 타임아웃은 DEFAULT_RESPONSE_TIMEOUT_MS = 60000ms로 정의되어 있습니다. 긴 동작에는 적절히 타임아웃을 늘려 사용하세요.

자발 메시지(Spontaneous): 장비의 자발 전송 메시지(에러/경고)는 Dispatcher의 spontaneous 핸들러로 전달됩니다. EMG 등 긴급 상황은 즉시 로그/알람 처리 및 안전 절차를 수행하세요.
