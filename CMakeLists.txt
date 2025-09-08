cmake_minimum_required(VERSION 3.10)
project(kohzu-controller LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
if(NOT Boost_FOUND)
  message(FATAL_ERROR "Boost not found. Install boost-devel")
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
	${CMAKE_SOURCE_DIR}/src/*.cpp
)

add_executable(kohzu_controller ${SOURCES})

target_include_directories(kohzu_controller PRIVATE include)
target_link_libraries(kohzu_controller PRIVATE Boost::system pthread)

