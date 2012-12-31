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

cmake_minimum_required(VERSION 2.8)
project(lockless)

enable_testing()

add_definitions("-std=c++11 -Wall -Wextra -Werror -pedantic -g -O4")
include_directories("${PROJECT_SOURCE_DIR}")


#------------------------------------------------------------------------------#
# TCMALLOC
#------------------------------------------------------------------------------#

option(USE_TCMALLOC "Use tcmalloc for heap allocations." ON)
find_library(LIB_TCMALLOC tcmalloc)

if(USE_TCMALLOC)
  if(LIB_TCMALLOC MATCHES "NOTFOUND")
    message("WARNING: tcmalloc is unavailable.")
  else()
    link_libraries(tcmalloc)
  endif()
endif()


#------------------------------------------------------------------------------#
# BUILD
#------------------------------------------------------------------------------#

add_library(lockless_log log.cpp)

add_subdirectory(tests)
