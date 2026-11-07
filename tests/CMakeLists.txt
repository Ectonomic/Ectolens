include_directories(${PROJECT_SOURCE_DIR}/src)

SET(FILENAME_TEST_SRCS
    FilenameTest.cpp
    ../src/ShortcutActions.cpp
    ../src/ExportManager.cpp
    ../src/Platforms/ImagePlatform.cpp
    ../src/Platforms/VideoPlatform.cpp
)

ecm_qt_declare_logging_category(FILENAME_TEST_SRCS
    HEADER spectacle_debug.h
    IDENTIFIER SPECTACLE_LOG
    CATEGORY_NAME spectacle
    DESCRIPTION "spectacle (general)"
    EXPORT SPECTACLE
)

kconfig_add_kcfg_files(FILENAME_TEST_SRCS GENERATE_MOC ${PROJECT_SOURCE_DIR}/src/Gui/SettingsDialog/settings.kcfgc)

ecm_add_test(
    ${FILENAME_TEST_SRCS}
    TEST_NAME "filename_test"
    LINK_LIBRARIES  Qt::Test
        Qt::PrintSupport Qt::Qml KF6::I18n KF6::ConfigCore KF6::GlobalAccel KF6::KIOCore KF6::WindowSystem KF6::XmlGui KF6::GuiAddons KF6::PrisonScanner
)

# Ectolens: what each recording quality choice asks of the encoder
ecm_add_test(
    RecordingQualityTest.cpp
    ../src/Platforms/RecordingQuality.cpp
    TEST_NAME "recording_quality_test"
    LINK_LIBRARIES Qt::Test K::KPipeWireRecord
)

# Ectolens: Hide Private Info's rule, held to examples both ways
ecm_add_test(
    PrivateInfoTest.cpp
    ../src/PrivateInfo.cpp
    TEST_NAME "private_info_test"
    LINK_LIBRARIES Qt::Test Qt::Gui PkgConfig::TESSERACT
)
target_compile_options(private_info_test PRIVATE -fexceptions)
target_compile_definitions(private_info_test PRIVATE ECTOLENS_FONTS_DIR="${PROJECT_SOURCE_DIR}/src/fonts")
set_tests_properties(private_info_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Ectolens: the Text and Number marks' look (Ecto green label and badge), drawn and read back
ecm_add_test(
    AnnotationLookTest.cpp
    TEST_NAME "annotation_look_test"
    LINK_LIBRARIES Qt::Test Qt::Gui Qt::Qml Qt::Quick EctolensImageEditor EctolensImageEditorPlugin KF6::ConfigCore KF6::I18n KF6::I18nQml
)
target_compile_definitions(annotation_look_test PRIVATE ECTOLENS_FONTS_DIR="${PROJECT_SOURCE_DIR}/src/fonts")
set_tests_properties(annotation_look_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Ectolens: the README's pictures: each one it shows is there, says what it shows, and is a cut
# that tools/readme-pictures.py makes again from a filmed scene; nothing else is in docs/pictures
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME readme_pictures_test COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/ReadmePicturesTest.py ${PROJECT_SOURCE_DIR})
    # starting the MCP server (as every agent session does) must not start Ectolens; a tool call may
    add_test(NAME mcp_starts_nothing_test COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/McpStartsNothingTest.py $<TARGET_FILE:ectolens-mcp>)
endif()

# Ectolens: records a PipeWire node with a quality choice, for tests/e2e/record-synthetic.sh
add_executable(ectolens-record-synthetic e2e/RecordSynthetic.cpp ../src/Platforms/RecordingQuality.cpp)
target_link_libraries(ectolens-record-synthetic PRIVATE Qt::Core K::KPipeWireRecord)

# Ectolens: a stand-in for KWin's screen cast, for tests/e2e/record-synthetic.sh
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
add_executable(ectolens-screen-source e2e/screen-source.c)
target_link_libraries(ectolens-screen-source PRIVATE PkgConfig::PIPEWIRE)

# Ectolens: a Spectacle user's settings come along once
add_test(NAME settings_copy_test COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/e2e/settings-copy.sh $<TARGET_FILE_DIR:ectolens>)


# Ectolens: real pointer and keyboard input for tests/e2e/overlay, through KWin's fake-input protocol
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
set(FAKE_INPUT_XML ${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml)
add_custom_command(OUTPUT fake-input-client-protocol.h fake-input-protocol.c
    COMMAND ${WAYLAND_SCANNER} client-header ${FAKE_INPUT_XML} fake-input-client-protocol.h
    COMMAND ${WAYLAND_SCANNER} private-code ${FAKE_INPUT_XML} fake-input-protocol.c
    DEPENDS ${FAKE_INPUT_XML})
add_executable(ectolens-fake-input e2e/overlay/fake-input.c ${CMAKE_CURRENT_BINARY_DIR}/fake-input-protocol.c)
target_include_directories(ectolens-fake-input PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(ectolens-fake-input PRIVATE PkgConfig::WAYLAND_CLIENT)
# Ectolens: times the compositor's answers from outside it (a frozen desktop shows as a slow answer)
add_executable(ectolens-compositor-watch e2e/overlay/compositor-watch.c)
target_link_libraries(ectolens-compositor-watch PRIVATE PkgConfig::WAYLAND_CLIENT)
