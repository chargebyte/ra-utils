install(
    FILES
        chargesom_fw_v_00_02_06.bin
        chargesom_parameter-block_only-contactor.yaml
        parsley_fw_v_00_02_06.bin
        parsley_parameter-block_factory-default.yaml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}
)

install(
    FILES
        ra-update.service
    DESTINATION /lib/systemd/system
)

install(
    PROGRAMS
        ra-update.sh
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}
)
