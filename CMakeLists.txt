cmake_minimum_required(VERSION 3.10)
project(kohzu-controller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 수정: Boost COMPONENTS에 asio와 thread 추가 (Asio 멀티스레드 지원). 에러 핸들링 강화.
find_package(Boost REQUIRED COMPONENTS system asio thread)
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost not found. Install boost-devel")
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
	${CMAKE_SOURCE_DIR}/src/*.cpp
)

add_executable(kohzu_controller ${SOURCES})

target_include_directories(kohzu_controller PRIVATE include)
# 수정: Boost::thread 추가로 스레드 안전성 강화.
target_link_libraries(kohzu_controller PRIVATE Boost::system Boost::thread pthread)
