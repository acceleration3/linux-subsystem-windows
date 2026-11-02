set(LSW_GUEST_UPDATE_DIR "${CMAKE_BINARY_DIR}/guest-update")

add_custom_command(
    OUTPUT "${LSW_GUEST_UPDATE_DIR}/lsw-guest-update.ps1"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/src/guest/update/lsw-guest-update.ps1"
            "${LSW_GUEST_UPDATE_DIR}/lsw-guest-update.ps1"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/guest/update/lsw-guest-update.ps1"
    COMMENT "Guest update: lsw-guest-update.ps1")
add_custom_target(lsw_guest_update ALL DEPENDS "${LSW_GUEST_UPDATE_DIR}/lsw-guest-update.ps1")
if(TARGET lsw-manager)
    add_dependencies(lsw-manager lsw_guest_update)
endif()

install(FILES "${CMAKE_SOURCE_DIR}/src/guest/update/lsw-guest-update.ps1"
        DESTINATION ${LSW_INSTALL_GUESTDIR}
        COMPONENT ${LSW_INSTALL_COMPONENT})
