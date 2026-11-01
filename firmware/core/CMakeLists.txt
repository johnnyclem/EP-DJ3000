# Portable firmware core: no ESP-IDF, no heap in hot paths, no exceptions/RTTI
# (mirrors ESP-IDF defaults so host builds catch what the chip would reject).
# Drop new .cpp files into src/ and test_*.cpp into test/: they are globbed.
cmake_minimum_required(VERSION 3.16)
project(epdj_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

option(EPDJ_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
if(EPDJ_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
add_compile_options(-fno-exceptions -fno-rtti)

file(GLOB CORE_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(epdj_core STATIC ${CORE_SRC})
target_include_directories(epdj_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(epdj_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wsign-conversion -Werror)

enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test/test_*.cpp)
foreach(t ${TEST_SRC})
  get_filename_component(n ${t} NAME_WE)
  add_executable(${n} ${t} ${CMAKE_CURRENT_SOURCE_DIR}/test/harness_main.cpp)
  target_link_libraries(${n} PRIVATE epdj_core)
  target_include_directories(${n} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
  target_compile_options(${n} PRIVATE -Wall -Wextra)
  add_test(NAME ${n} COMMAND ${n})
endforeach()
