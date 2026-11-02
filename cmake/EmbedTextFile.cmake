function(embed_text_file SOURCE OUTPUT VARNAME)
    if(NOT EXISTS "${SOURCE}")
        message(FATAL_ERROR "embed_text_file: missing source ${SOURCE}")
    endif()
    file(READ "${SOURCE}" _contents)
    if(_contents MATCHES "LSW_EMBED")
        message(FATAL_ERROR
            "embed_text_file: the delimiter 'LSW_EMBED' appears inside ${SOURCE}; "
            "raw-string embedding would break.")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SOURCE}")
    file(WRITE "${OUTPUT}"
"// Auto-generated from ${SOURCE} by embed_text_file().
// Do not edit by hand.
#pragma once
namespace lsw::embedded {
inline constexpr char ${VARNAME}[] = R\"LSW_EMBED(${_contents})LSW_EMBED\";
}  // namespace lsw::embedded
")
endfunction()
