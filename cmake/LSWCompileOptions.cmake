include_guard(GLOBAL)

add_library(lsw_compile_options INTERFACE)
add_library(LSW::CompileOptions ALIAS lsw_compile_options)

target_compile_features(lsw_compile_options INTERFACE cxx_std_20)

if(MSVC)
    target_compile_options(lsw_compile_options INTERFACE
        /W4
        /permissive-
        /Zc:preprocessor
        /Zc:__cplusplus
        /utf-8
        /EHsc
    )
    target_compile_definitions(lsw_compile_options INTERFACE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
else()
    target_compile_options(lsw_compile_options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
    )
endif()

# Sanitizer options (opt-in)
option(LSW_ENABLE_ASAN "Enable AddressSanitizer (Debug builds, non-MSVC)" OFF)
option(LSW_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (non-MSVC)" OFF)

if(LSW_ENABLE_ASAN AND NOT MSVC)
    target_compile_options(lsw_compile_options INTERFACE -fsanitize=address)
    target_link_options(lsw_compile_options INTERFACE -fsanitize=address)
endif()

if(LSW_ENABLE_UBSAN AND NOT MSVC)
    target_compile_options(lsw_compile_options INTERFACE -fsanitize=undefined)
    target_link_options(lsw_compile_options INTERFACE -fsanitize=undefined)
endif()
