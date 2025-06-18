install(
    FILES
        chargeSOM_fw_v_00_02_02.bin
        chargeSOM_parameter-block_only-contactor.bin
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
