add_library(suite_selftest OBJECT selftest.cpp)
target_link_libraries(suite_selftest PUBLIC suite_core)
