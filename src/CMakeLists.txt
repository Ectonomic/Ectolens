# common - configure file and version definitions

set(SPECTACLE_QML_URI "org.kde.spectacle.private")
set(SPECTACLE_QML_PATH "qrc:/qt/qml/org/kde/spectacle/private")
configure_file(Config.h.in ${CMAKE_CURRENT_BINARY_DIR}/Config.h)

# target

kconfig_add_kcfg_files(SPECTACLE_SRCS GENERATE_MOC Gui/SettingsDialog/settings.kcfgc)

ecm_qt_declare_logging_category(SPECTACLE_SRCS
    HEADER spectacle_debug.h
    IDENTIFIER SPECTACLE_LOG
    CATEGORY_NAME spectacle
    DESCRIPTION "spectacle (general)"
    EXPORT SPECTACLE
)

add_executable(ectolens)
qt_add_qml_module(ectolens URI ${SPECTACLE_QML_URI} DEPENDENCIES QtCore QtQuick IMPORT_PATH "${CMAKE_BINARY_DIR}/qml")

target_sources(ectolens PRIVATE
    ${SPECTACLE_SRCS}
    AgentService.cpp
    CaptureModeModel.cpp
    CommandLineOptions.cpp
    ExportManager.cpp
    Geometry.cpp
    OcrManager.cpp
    Gui/CaptureTools.cpp
    Gui/CaptureWindow.cpp
    Gui/FrameHistory.cpp
    Gui/PinWindow.cpp
    Gui/ExportMenu.cpp
    Gui/HelpMenu.cpp
    Gui/InlineMessageModel.cpp
    Gui/OptionsMenu.cpp
    TestScript.cpp
    Gui/Selection.cpp
    Gui/SelectionEditor.cpp
    Gui/SettingsDialog/AppearanceOptionsPage.cpp
    Gui/SettingsDialog/AgentsOptionsPage.cpp
    Gui/SettingsDialog/GeneralOptionsPage.cpp
    Gui/SettingsDialog/ImageSaveOptionsPage.cpp
    Gui/SettingsDialog/OcrLanguageSelector.cpp
    Gui/SettingsDialog/SettingsDialog.cpp
    Gui/SettingsDialog/ShortcutsOptionsPage.cpp
    Gui/SettingsDialog/VideoFormatComboBox.cpp
    Gui/SettingsDialog/VideoSaveOptionsPage.cpp
    Gui/SettingsDialog/RecordingOptionsPage.cpp
    Gui/SmartSpinBox.cpp
    Gui/SpectacleMenu.cpp
    Gui/SpectacleWindow.cpp
    Gui/TextContextMenu.cpp
    Gui/ViewerWindow.cpp
    Main.cpp
    PlasmaVersion.cpp
    PrivateInfo.cpp
    Platforms/ImagePlatform.cpp
    Platforms/ImagePlatformKWin.cpp
    Platforms/PlatformLoader.cpp
    Platforms/PlatformNull.cpp
    Platforms/screencasting.cpp
    Platforms/VideoPlatform.cpp
    Platforms/VideoPlatformWayland.cpp
    Platforms/WindowPicker.cpp
    Platforms/RecordingQuality.cpp
    RecordingModeModel.cpp
    ScreenShotEffect.cpp
    ShortcutActions.cpp
    SpectacleCore.cpp
    StartupClock.cpp
    SpectacleDBusAdapter.cpp
    VideoFormatModel.cpp
)

if(WITH_X11)
    target_sources(ectolens PRIVATE Platforms/ImagePlatformXcb.cpp Platforms/ImagePlatformXcb.h)

    target_link_libraries(ectolens PRIVATE
        XCB::XFIXES
        XCB::IMAGE
        XCB::CURSOR
        XCB::UTIL
        XCB::RANDR
    )
    target_link_libraries(ectolens PRIVATE Qt6::GuiPrivate) # Gui/private/qtx11extras_p.h
endif()

qt6_generate_wayland_protocol_client_sources(ectolens
    PRIVATE_CODE
    FILES
        ${PLASMA_WAYLAND_PROTOCOLS_DIR}/zkde-screencast-unstable-v1.xml
        ${PLASMA_WAYLAND_PROTOCOLS_DIR}/plasma-window-management.xml
)

if(PURPOSE_FOUND)
    target_link_libraries(ectolens PRIVATE KF6::PurposeWidgets)
endif()

ki18n_wrap_ui(ectolens
    Gui/SettingsDialog/GeneralOptions.ui
    Gui/SettingsDialog/ImageSaveOptions.ui
    Gui/SettingsDialog/VideoSaveOptions.ui
)

# Needed to compile with Tesseract
target_compile_options (ectolens PRIVATE -fexceptions)

target_link_libraries(ectolens PRIVATE
    Qt::Concurrent
    Qt::DBus
    Qt::PrintSupport
    Qt::Qml
    Qt::Quick
    Qt::QuickControls2
    Qt::QuickTemplates2
    Qt::GuiPrivate
    Qt::WaylandClient
    KF6::CoreAddons
    KF6::DBusAddons
    KF6::WidgetsAddons
    KF6::Notifications
    KF6::ConfigCore
    KF6::I18n
    KF6::KIOWidgets
    KF6::WindowSystem
    KF6::GlobalAccel
    KF6::XmlGui
    KF6::GuiAddons
    KF6::KirigamiPlatform
    KF6::StatusNotifierItem
    KF6::PrisonScanner
    KF6::Crash
    KF6::I18nQml
    K::KPipeWireRecord
    Wayland::Client
    LayerShellQt::Interface
    EctolensImageEditor
    EctolensImageEditorPlugin
    PkgConfig::TESSERACT
)

# qt_add_qml_module doesn't know how to deal with headers in subdirectories so
# make sure to add those so the headers can be found.
target_include_directories(ectolens PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gui>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Platforms>"
)

set_source_files_properties(Gui/QmlUtils.qml Gui/EctoTheme.qml PROPERTIES
  QT_QML_SINGLETON_TYPE TRUE
)

# Ectolens: Geist and Geist Mono ship inside the app (SIL Open Font License 1.1, src/fonts/README.md).
qt_add_resources(ectolens "ectolens_fonts"
    PREFIX "/"
    FILES
    fonts/Geist-Regular.ttf
    fonts/Geist-Medium.ttf
    fonts/Geist-SemiBold.ttf
    fonts/GeistMono-Regular.ttf
    fonts/GeistMono-Medium.ttf
    fonts/OFL.txt
)
# Ectolens: the gear, drawn in Breeze's manner (Breeze has no plain gear; design/mock/icons/gear.svg).
qt_add_resources(ectolens "ectolens_ui_icons"
    PREFIX "/icons"
    BASE Gui
    FILES
    Gui/ectolens-gear.svg
)

qt_target_qml_sources(ectolens
    QML_FILES
    Gui/AfterCaptureMenu.qml
    Gui/EctoDelay.qml
    Gui/EctoSwatch.qml
    Gui/EctoColorMenu.qml
    Gui/EctoFontMenu.qml
    Gui/EctoToolOptions.qml
    Gui/EctoTools.qml
    Gui/FrameHandles.qml
    Gui/FrameLabel.qml
    Gui/OverlayOptionsMenu.qml
    Gui/RecordPanel.qml
    Gui/SizeMenu.qml
    Gui/EctoButton.qml
    Gui/EctoCheck.qml
    Gui/EctoChevron.qml
    Gui/EctoKey.qml
    Gui/EctoLabel.qml
    Gui/EctoToolTip.qml
    Gui/EctoMenu.qml
    Gui/EctoMenuHeader.qml
    Gui/EctoMenuItem.qml
    Gui/EctoMenuRow.qml
    Gui/EctoMenuSeparator.qml
    Gui/EctoPick.qml
    Gui/EctoRow.qml
    Gui/EctoSegmented.qml
    Gui/EctoSeparator.qml
    Gui/EctoSlider.qml
    Gui/EctoSplitButton.qml
    Gui/EctoTheme.qml
    Gui/PinView.qml
    Gui/AnimatedLoader.qml
    Gui/AnnotationEditor.qml
    Gui/CaptureOverlay.qml
    Gui/DashedOutline.qml
    Gui/EmptyPage.qml
    Gui/FloatingBackground.qml
    Gui/FloatingToolBar.qml
    Gui/Handle.qml
    Gui/Magnifier.qml
    Gui/Outline.qml
    Gui/QmlUtils.qml
    Gui/RecordingView.qml
    Gui/ScreenshotView.qml
    Gui/TtToolButton.qml
    Gui/ViewerPage.qml
)

install(TARGETS ectolens ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Ectolens: the MCP server AI agents start (ectolens-mcp); it asks Ectolens over D-Bus (AgentService)
add_executable(ectolens-mcp mcp/EctolensMcp.cpp)
target_compile_definitions(ectolens-mcp PRIVATE ECTOLENS_VERSION="${PROJECT_VERSION}")
target_link_libraries(ectolens-mcp PRIVATE Qt::Core Qt::DBus)
install(TARGETS ectolens-mcp ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES Gui/SettingsDialog/spectacle.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})
