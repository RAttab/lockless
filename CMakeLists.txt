#------------------------------------------------------------------------------#
# CMakeLists.txt
# Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
# FreeBSD-style copyright and disclaimer apply
#
# Blah
#------------------------------------------------------------------------------#


#------------------------------------------------------------------------------#
# CONFIG
#------------------------------------------------------------------------------#

cmake_minimum_required(VERSION 2.6)
project(lockless)

enable_testing()

add_definitions("-std=c++11 -Wall -Wextra -Werror -pedantic -g -O4")
include_directories("${PROJECT_SOURCE_DIR}/src")

link_libraries(rt)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#------------------------------------------------------------------------------#
# TCMALLOC
#------------------------------------------------------------------------------#

option(USE_TCMALLOC "Use tcmalloc for heap allocations." ON)
find_library(LIB_TCMALLOC tcmalloc)

if(USE_TCMALLOC)
    if(LIB_TCMALLOC MATCHES "NOTFOUND")
        message("WARNING: unable to find tcmalloc")
    else()
        link_libraries(tcmalloc)
        add_definitions("-fno-builtin-malloc -fno-builtin-calloc")
        add_definitions("-fno-builtin-realloc -fno-builtin-free")
    endif()
endif()


#------------------------------------------------------------------------------#
# BUILD
#------------------------------------------------------------------------------#

install(
    FILES
    src/utils.h src/log.h src/check.h
    src/clock.h src/magic.h src/atomizer.h
    src/rcu.h
    src/map.h map.tcc

    DESTINATION
    include/lockless)

add_library(lockless_log SHARED src/log.cpp)
install(TARGETS lockless_log DESTINATION lib)

#------------------------------------------------------------------------------#
# TESTS
#------------------------------------------------------------------------------#

add_executable(rcu_test tests/rcu_test.cpp)
target_link_libraries(rcu_test lockless_log boost_unit_test_framework)
add_test(rcu rcu_test)

add_executable(rcu_perf_test tests/rcu_perf_test.cpp)
target_link_libraries(rcu_perf_test lockless_log)

add_executable(log_test tests/log_test.cpp)
target_link_libraries(log_test lockless_log boost_unit_test_framework)
add_test(log log_test)

add_executable(map_test tests/map_test.cpp)
target_link_libraries(map_test lockless_log boost_unit_test_framework)
add_test(map map_test)

add_executable(map_para_test tests/map_para_test.cpp)
target_link_libraries(map_para_test lockless_log boost_unit_test_framework)
add_test(map_para map_para_test)
