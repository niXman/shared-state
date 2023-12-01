cmake_minimum_required(VERSION 3.5)

project(asio-shared-state-client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=address -fsanitize=leak") # -fsanitize=thread
endif()

add_executable(${PROJECT_NAME} client.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE "../cmdargs/include")

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/bin" #${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION "${CMAKE_CURRENT_SOURCE_DIR}/bin" #${CMAKE_INSTALL_BINDIR}
)
