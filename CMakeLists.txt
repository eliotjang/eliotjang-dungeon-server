cmake_minimum_required(VERSION 3.21)

project(ejd_server VERSION 0.1.0 LANGUAGES CXX)

#---- 전역 : 언어 표준 ----
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # -std=c++20

#---- 컨벤션 ----
set(EJD_STRICT_OPTIONS -Wall -Wextra -Werror -fno-exceptions -fno-rtti)

#---- ejd_lib : 서버 코드 전체 ----
add_library(ejd_lib STATIC
    src/common/version.cc
    src/net/socket_util.cc
    src/net/session.cc
    src/net/epoll_reactor.cc
    src/net/ring_buffer.cc
    src/net/framing.cc
)
# ejd_server, ejd_tests도 src include 경로 사용
target_include_directories(ejd_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(ejd_lib PRIVATE ${EJD_STRICT_OPTIONS})
# 프로젝트 버전 주입
target_compile_definitions(ejd_lib PRIVATE EJD_VERSION="${PROJECT_VERSION}")

#---- ejd_server : 실행 파일 ----
add_executable(ejd_server src/main.cc)
target_link_libraries(ejd_server PRIVATE ejd_lib)
target_compile_options(ejd_server PRIVATE ${EJD_STRICT_OPTIONS})

#---- ejd_tests : GTest 스모크 ----
enable_testing() # CTest 사용

include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

add_executable(ejd_tests
    tests/smoke_test.cc
    tests/ring_buffer_test.cc
    tests/framing_test.cc
)
# 예외 및 런타임 타입 정보 사용 가능
target_compile_options(ejd_tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ejd_tests PRIVATE ejd_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(ejd_tests)

#---- ejd_bot : 부하 검증 봇 클라이언트----
add_executable(ejd_bot botclient/main.cc)
target_link_libraries(ejd_bot PRIVATE ejd_lib)
target_compile_options(ejd_bot PRIVATE ${EJD_STRICT_OPTIONS})
