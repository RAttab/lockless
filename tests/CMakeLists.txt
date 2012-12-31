#------------------------------------------------------------------------------#
# CMakeLists.txt
# Rémi Attab (remi.attab@gmail.com), 30 Dec 2012
# FreeBSD-style copyright and disclaimer apply
#
# Build definitions for the tests.
#------------------------------------------------------------------------------#

add_executable(rcu_test rcu_test.cpp)
target_link_libraries(rcu_test lockless_log boost_unit_test_framework)
add_test(rcu rcu_test)