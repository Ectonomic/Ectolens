# install the DBus service and interface files in the correct place
include(ECMGenerateDBusServiceFile)
include(ECMConfiguredInstall)

# Generate and install dbus-activated systemd service
ecm_install_configured_files(INPUT app-com.ectonomic.ectolens.service.in DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})

ecm_generate_dbus_service_file(
   NAME com.ectonomic.ectolens
   EXECUTABLE "${KDE_INSTALL_FULL_BINDIR}/ectolens --dbus"
   SYSTEMD_SERVICE app-com.ectonomic.ectolens.service
   DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
)
ecm_generate_dbus_service_file(
   NAME com.ectonomic.Ectolens
   EXECUTABLE "${KDE_INSTALL_FULL_BINDIR}/ectolens --dbus"
   SYSTEMD_SERVICE app-com.ectonomic.ectolens.service
   DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
)

install(FILES com.ectonomic.Ectolens.xml DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})
