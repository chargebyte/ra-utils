install(
    FILES
        chargesom_fw_v_00_02_09.bin
        chargesom_parameter-block_only-contactor.yaml
        parsley_fw_v_00_02_09.bin
        parsley_parameter-block_factory-default.yaml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}
)

install(
    FILES
        ra-update@.service
    DESTINATION /lib/systemd/system
)

install(
    DIRECTORY
        ra-update@ttyLP1.service.d
        ra-update@ttyLP2.service.d
    DESTINATION /lib/systemd/system
    FILES_MATCHING
    PATTERN "*.conf"
)

install(
    PROGRAMS
        ra-update.sh
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
)
