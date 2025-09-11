cmake_minimum_required(VERSION 3.10)
project(kohzu-controller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Boost 찾기: system 구성 요소만 필요
find_package(Boost REQUIRED COMPONENTS system)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found. Please install Boost (e.g., sudo apt-get install libboost-all-dev on Ubuntu)")
endif()

# 소스 파일 목록 명시적 정의
set(SOURCES
    src/app/cli_main.cpp
    src/comm/AsioTcpClient.cpp
    src/comm/Writer.cpp
    src/controller/KohzuManager.cpp
    src/controller/MotorController.cpp
    src/controller/StateCache.cpp
    src/controller/Poller.cpp
    src/protocol/Dispatcher.cpp
    src/protocol/Parser.cpp
)

# 실행 파일 생성
add_executable(kohzu_controller ${SOURCES})

# 인클루드 디렉토리 설정
target_include_directories(kohzu_controller PRIVATE include)

# 링크 라이브러리: Boost::system과 pthread
target_link_libraries(kohzu_controller PRIVATE Boost::system pthread)

# 컴파일 옵션 추가: 최적화 및 경고 활성화
target_compile_options(kohzu_controller PRIVATE -O2 -Wall)

# 설치 타겟 (선택적)
install(TARGETS kohzu_controller DESTINATION bin)