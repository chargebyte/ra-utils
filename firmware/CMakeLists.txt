install(
    FILES
        chargesom_fw_v_00_04_01.bin
        chargesom_parameter-block_factory-default.yaml
        parsley_fw_v_00_05_01.bin
        parsley_parameter-block_factory-default.yaml
        ttyLP1.env
        ttyLP2.env
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}
)

foreach(service ra-update ra-parameter-dump)
    configure_file(${service}@.service.in ${service}@.service @ONLY)
endforeach()

configure_file(factory-reset.in factory-reset @ONLY)
install(
    PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/factory-reset
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/factory-reset.d
    RENAME 50_ra-utils
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/ra-update@.service
        ${CMAKE_CURRENT_BINARY_DIR}/ra-parameter-dump@.service
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
        ra-parameter-dump@ttyLP1.service.d
        ra-parameter-dump@ttyLP2.service.d
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
