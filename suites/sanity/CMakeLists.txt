add_library(suite_sanity OBJECT
    sanity.cpp
    env/av_display_devices.cpp
    env/adapters_present.cpp
)
target_link_libraries(suite_sanity PUBLIC suite_core)
