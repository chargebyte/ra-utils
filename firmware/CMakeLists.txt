install(
    FILES
        chargesom_fw_v_00_02_02.bin
        chargesom_parameter-block_only-contactor.bin
        parsley_fw_v_00_02_02.bin
        parsley_parameter-block_all-pt1000-disabled.bin
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
