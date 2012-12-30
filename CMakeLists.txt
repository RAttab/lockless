#------------------------------------------------------------------------------#
# CMakeLists.txt
# Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
# FreeBSD-style copyright and disclaimer apply
#
# Blah
#------------------------------------------------------------------------------#

cmake_minimum_required(VERSION 2.8)
project(lockless)

enable_testing()

add_definitions("-std=c++11 -Wall -Wextra -Werror -pedantic -O4")
include_directories("${PROJECT_SOURCE_DIR}")

add_library(lockless_log log.cpp)

add_subdirectory(tests)
