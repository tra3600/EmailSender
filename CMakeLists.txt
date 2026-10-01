cmake_minimum_required(VERSION 3.14)
project(EmailSender VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(CURL REQUIRED)

add_library(emailsender STATIC
  src/Mime.cpp src/Message.cpp src/Template.cpp src/Config.cpp src/EmailSender.cpp)
target_include_directories(emailsender PUBLIC src)
target_link_libraries(emailsender PUBLIC CURL::libcurl)
if(MSVC)
  target_compile_options(emailsender PRIVATE /W4)
else()
  target_compile_options(emailsender PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(emailsender_cli src/main.cpp)
set_target_properties(emailsender_cli PROPERTIES OUTPUT_NAME emailsender)
target_link_libraries(emailsender_cli PRIVATE emailsender)

enable_testing()
add_executable(unit_tests tests/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE emailsender)
add_test(NAME unit_tests COMMAND unit_tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME smtp_integration
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/integration_test.py $<TARGET_FILE:emailsender_cli>)
endif()

install(TARGETS emailsender_cli RUNTIME DESTINATION bin)
