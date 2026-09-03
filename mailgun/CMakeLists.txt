cmake_minimum_required(VERSION 3.20)
project(slugkit-mailgun CXX)


set(${PROJECT_NAME}_SRC
    src/slugkit/mailgun/endpoints/client_mailgun.cpp
)

set(${PROJECT_NAME}_HEADERS
    include/slugkit/geo/endpoints/client_geo.hpp
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
