cmake_minimum_required(VERSION 3.10)
project(kohzu-controller VERSION 1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Boost 라이브러리 찾기 (asio, system)
find_package(Boost REQUIRED)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found. Please install Boost libraries.")
endif()

# spdlog 라이브러리 찾기
find_package(spdlog REQUIRED)
if(NOT spdlog_FOUND)
    message(FATAL_ERROR "spdlog not found. Please install spdlog library.")
endif()

# 헤더 파일 경로 설정
include_directories(include)

# 소스 파일 목록
set(SOURCE_FILES
    src/app/main.cpp
    src/controller/KohzuController.cpp
    src/controller/AxisState.cpp
    src/core/TcpClient.cpp
    src/protocol/ProtocolHandler.cpp
    src/protocol/exceptions/ConnectionException.cpp
    src/protocol/exceptions/ProtocolException.cpp
    src/protocol/exceptions/TimeoutException.cpp
    src/common/ThreadSafeQueue.cpp
)

# 실행 파일 생성
add_executable(kohzu_controller ${SOURCE_FILES})

# 라이브러리 링크
target_link_libraries(kohzu_controller PRIVATE
    Boost::boost
    spdlog::spdlog
)

# Boost의 정적 라이브러리 사용 시, thread 라이브러리 추가 링크 (CMake가 자동으로 하지 않을 수 있음)
if(WIN32)
    set(CMAKE_THREAD_LIBS_INIT "-lwsock32 -lws2_32 -lboost_thread")
else()
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
endif()
