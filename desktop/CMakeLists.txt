# install the .desktop and rc files in the correct place

configure_file(com.ectonomic.ectolens.desktop.cmake ${CMAKE_CURRENT_BINARY_DIR}/com.ectonomic.ectolens.desktop)
install(
    PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/com.ectonomic.ectolens.desktop
    DESTINATION ${KDE_INSTALL_APPDIR}
)

install( DIRECTORY DESTINATION "${KDE_INSTALL_FULL_DATAROOTDIR}/kglobalaccel" )
install(
    CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"${KDE_INSTALL_FULL_APPDIR}/com.ectonomic.ectolens.desktop\" \"\$ENV{DESTDIR}${KDE_INSTALL_FULL_DATAROOTDIR}/kglobalaccel/com.ectonomic.ectolens.desktop\")"
)

install(
    FILES ectolens.notifyrc
    DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR}
)

install(
    FILES com.ectonomic.ectolens.appdata.xml
    DESTINATION ${KDE_INSTALL_METAINFODIR}
)
