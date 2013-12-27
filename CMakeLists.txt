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
link_libraries(pthread)

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
    src/debug.h src/utils.h src/log.h src/check.h src/tls.h
    src/clock.h src/magic.h src/atomizer.h
    src/rcu.h src/grcu.h src/snzi.h
    src/list.h src/queue.h
    src/map.h src/map.tcc

    DESTINATION
    include/lockless)

add_library(
    lockless
    SHARED
    src/tls.cpp src/log.cpp src/check.cpp src/alloc.cpp
    src/grcu.cpp)

install(TARGETS lockless DESTINATION lib)


#------------------------------------------------------------------------------#
# TESTS
#------------------------------------------------------------------------------#

function(lockless_test name)
    add_executable(${name}_test tests/${name}_test.cpp)
    target_link_libraries(${name}_test lockless boost_unit_test_framework)
    add_test(${name} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${name}_test)
endfunction()


lockless_test(log)
lockless_test(log_para)

lockless_test(tls)
lockless_test(tls_perf)

lockless_test(list)
lockless_test(list_para)

lockless_test(snzi)
lockless_test(snzi_para)
lockless_test(snzi_perf)

lockless_test(grcu)
lockless_test(grcu_para)

lockless_test(rcu)
lockless_test(rcu_para)
lockless_test(rcu_perf)

lockless_test(queue)
lockless_test(queue_para)
lockless_test(queue_perf)

lockless_test(map)
lockless_test(map_para)

lockless_test(arena)
lockless_test(arena_para)

lockless_test(alloc)
lockless_test(alloc_para)
lockless_test(alloc_perf)

lockless_test(lock)
lockless_test(lock_para)
lockless_test(lock_perf)

lockless_test(ring)
lockless_test(ring_para)
