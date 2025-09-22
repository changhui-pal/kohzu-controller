# kohzu-controller 라이브러리 생성
add_library(kohzu-controller STATIC)

# 라이브러리에 포함될 소스 파일을 자동으로 찾습니다.
# file(GLOB_RECURSE ...)는 하위 폴더까지 모두 검색해 편리합니다.
file(GLOB_RECURSE CONTROLLER_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp"
)

target_sources(kohzu-controller PRIVATE ${CONTROLLER_SRCS})

# 라이브러리의 공개 헤더 파일 경로를 지정합니다.
# PUBLIC으로 설정하면, 이 라이브러리를 사용하는 다른 타겟(app)에서
# 자동으로 이 경로를 include 할 수 있게 됩니다.
target_include_directories(kohzu-controller
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# 이 라이브러리가 필요로 하는 다른 라이브러리들을 연결합니다.
target_link_libraries(kohzu-controller
    PUBLIC
        Boost::asio
        spdlog::spdlog
)
