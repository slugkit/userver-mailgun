cmake_minimum_required(VERSION 3.20)
project(slugkit-mailgun CXX)


set(${PROJECT_NAME}_SRC
    src/slugkit/mailgun/component.cpp
    src/slugkit/mailgun/types.cpp
)

set(${PROJECT_NAME}_HEADERS
    include/slugkit/mailgun/component.hpp
    include/slugkit/mailgun/fwd.hpp
    include/slugkit/mailgun/types.hpp
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
