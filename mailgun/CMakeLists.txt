cmake_minimum_required(VERSION 3.20)
project(slugkit-mailgun CXX)


set(${PROJECT_NAME}_SRC
    src/slugkit/mailgun/component.cpp
    src/slugkit/mailgun/metrics.cpp
    src/slugkit/mailgun/secrets.cpp
    src/slugkit/mailgun/types.cpp
    src/slugkit/mailgun/webhook.cpp
)

set(${PROJECT_NAME}_HEADERS
    include/slugkit/mailgun/component.hpp
    include/slugkit/mailgun/fwd.hpp
    include/slugkit/mailgun/metrics.hpp
    include/slugkit/mailgun/secrets.hpp
    include/slugkit/mailgun/types.hpp
    include/slugkit/mailgun/webhook.hpp
)

add_library(${PROJECT_NAME} STATIC ${${PROJECT_NAME}_SRC} ${${PROJECT_NAME}_HEADERS})

target_include_directories(
    ${PROJECT_NAME}
    PUBLIC
        include
)
target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
        userver::core
)

if(USERVER_MAILGUN_BUILD_TESTS)
    add_executable(slugkit-mailgun_unittest tests/metrics_test.cpp)
    target_link_libraries(slugkit-mailgun_unittest PRIVATE ${PROJECT_NAME} userver::utest)
    add_google_tests(slugkit-mailgun_unittest)
endif()
