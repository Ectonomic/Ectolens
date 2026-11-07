# Ectolens's icon (design/mock/_marks/build_icons.py): each small size at its own line weight, plus the scalable master.
# have ecm install them

ecm_install_icons(
    ICONS
        sc-apps-ectolens.svg
        16-apps-ectolens.svg
        22-apps-ectolens.svg
        24-apps-ectolens.svg
        32-apps-ectolens.svg
        48-apps-ectolens.svg
        64-apps-ectolens.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

qt_add_resources(ectolens "spectacle_icons"
    PREFIX "/icons"
    FILES
        256-status-media-recording-started.webp
        256-status-media-recording-pulse.webp
        256-status-media-recording.webp
)
