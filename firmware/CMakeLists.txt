install(
    FILES
        chargesom_fw_v_00_04_00.bin
        chargesom_parameter-block_only-contactor.yaml
        parsley_fw_v_00_04_00.bin
        parsley_parameter-block_factory-default.yaml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}
)

install(
    FILES
        ra-update@.service
        ra-parameter-dump@.service
    DESTINATION ${SYSTEMD_SYSTEM_UNITDIR}
)

pkg_get_variable(SYSTEMD_TMPFILESDIR systemd tmpfilesdir)
if(NOT SYSTEMD_TMPFILESDIR)
    set(SYSTEMD_TMPFILESDIR "${CMAKE_INSTALL_FULL_LIBDIR}/tmpfiles.d")
endif()

install(
    FILES
        ra-utils.conf
    DESTINATION ${SYSTEMD_TMPFILESDIR}
)

install(
    DIRECTORY
        ra-update@ttyLP1.service.d
        ra-update@ttyLP2.service.d
    DESTINATION ${SYSTEMD_SYSTEM_UNITDIR}
    FILES_MATCHING
    PATTERN "*.conf"
)

install(
    PROGRAMS
        ra-update.sh
        ra-parameter-dump.sh
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
)
