# SPDX-FileCopyrightText: 2020 Carl Schwan <carl@carlschwan.eu>
# SPDX-FileCopyrightText: 2025 Noah Davis <noahadvs@gmail.com>
# SPDX-FileCopyrightText: 2026 Ectolens contributors
# SPDX-License-Identifier: BSD-2-Clause
#
# Ectolens's own copy of KQuickImageEditor 0.7.0 (README.ectolens.md says what changed and why),
# built into the ectolens binary: a static library and a static QML module under its own name,
# org.kde.kquickimageeditor.ectolens, so Plasma's installed KQuickImageEditor is never loaded
# next to it. Upstream's src/CMakeLists.txt is the model; installing, docs, Highway and OpenCV are
# left out.

# Upstream builds without Ectolens's deprecation cutoff (resizehandle.cpp still uses
# QMouseEvent::windowPos), so lift it for this folder only.
get_directory_property(defs COMPILE_DEFINITIONS)
list(FILTER defs EXCLUDE REGEX "^QT_DISABLE_DEPRECATED_BEFORE=|^KF_DISABLE_DEPRECATED_BEFORE_AND_AT=")
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "${defs}")

add_library(EctolensImageEditor STATIC)

qt_add_qml_module(EctolensImageEditor
    URI "org.kde.kquickimageeditor.ectolens"
    VERSION 1.0
    CLASS_NAME KQuickImageEditorPlugin
    PLUGIN_TARGET EctolensImageEditorPlugin
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/org/kde/kquickimageeditor/ectolens"
    NO_PLUGIN_OPTIONAL
    DEPENDENCIES
        "QtQuick"
        "QtQuick.Controls"
        "QtQuick.Effects"
        "QtQuick.Shapes"
        "QtQuick.Templates"
        "org.kde.kirigami"
)

kconfig_target_kcfg_file(EctolensImageEditor
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/annotationconfig.kcfg"
    CLASS_NAME AnnotationConfig
    DEFAULT_VALUE_GETTERS
    GENERATE_MOC
    GENERATE_PROPERTIES
    MUTATORS
    QML_REGISTRATION
    SINGLETON
)

target_sources(EctolensImageEditor PRIVATE
    src/kquickimageeditor_export.h
    src/annotations/annotationdocument.cpp
    src/annotations/annotationdocument.h
    src/annotations/annotationdocument_p.h
    src/annotations/annotationtool.cpp
    src/annotations/annotationtool.h
    src/annotations/annotationviewport.cpp
    src/annotations/annotationviewport.h
    src/annotations/history.cpp
    src/annotations/history.h
    src/annotations/qmlpainterpath.cpp
    src/annotations/qmlpainterpath.h
    src/annotations/stackblur.cpp
    src/annotations/stackblur.h
    src/annotations/traits.cpp
    src/annotations/traits.h
    src/annotations/utils.h
    src/annotations/coloradjustment.cpp
    src/annotations/coloradjustment.h

    src/commands/undocommand.cpp
    src/commands/undocommand.h
    src/commands/cropcommand.cpp
    src/commands/cropcommand.h
    src/commands/resizecommand.cpp
    src/commands/resizecommand.h
    src/commands/mirrorcommand.cpp
    src/commands/mirrorcommand.h
    src/commands/rotatecommand.cpp
    src/commands/rotatecommand.h
    src/resizehandle.cpp
    src/resizehandle.h
    src/resizerectangle.cpp
    src/resizerectangle.h
    src/imageitem.cpp
    src/imageitem.h
    src/imagedocument.cpp
    src/imagedocument.h
)

# The QML files keep upstream's places in the module (TextTool.qml at its top,
# the helpers in private/), so their relative imports still work.
set(qml_public
    annotations/AnnotationSelectionTool.qml
    annotations/CropTool.qml
    annotations/HoverOutline.qml
    annotations/TextTool.qml
    controls/BasicResizeHandle.qml
    controls/CropBackground.qml
    controls/RectangleCutout.qml
    controls/SelectionBackground.qml
    controls/SelectionHandle.qml
    controls/SelectionTool.qml
)
set(qml_private
    annotations/private/AnimatedLoader.qml
    annotations/private/DashedOutline.qml
    annotations/private/Handle.qml
    annotations/private/Outline.qml
    annotations/private/TextContextMenu.qml
)
set(qml_files)
foreach(file IN LISTS qml_public)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(src/${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
    list(APPEND qml_files src/${file})
endforeach()
foreach(file IN LISTS qml_private)
    get_filename_component(name ${file} NAME)
    set_source_files_properties(src/${file} PROPERTIES QT_RESOURCE_ALIAS private/${name} QT_QML_INTERNAL_TYPE TRUE)
    list(APPEND qml_files src/${file})
endforeach()
qt_target_qml_sources(EctolensImageEditor QML_FILES ${qml_files})

target_link_libraries(EctolensImageEditor
    PUBLIC
    Qt6::Core
    Qt6::Quick
    Qt6::Qml
    PRIVATE
    Qt6::Concurrent
    KF6::ConfigCore
    KF6::ConfigGui
)

# The color adjustment shaders, compiled with Qt's qsb as upstream's qt6_add_shaders does
# (BATCHABLE vertex shader, OPTIMIZED; the default GLSL, HLSL and MSL targets plus SPIR-V).
# Called directly so the build needs only the qsb tool, not Qt ShaderTools' CMake package.
if(TARGET Qt6::qsb)
    set(ECTOLENS_QSB Qt6::qsb)
else()
    find_program(ECTOLENS_QSB NAMES qsb qsb-qt6 HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBEXECS}" REQUIRED)
endif()
set(shader_outputs)
foreach(stage IN ITEMS vert frag)
    set(in "${CMAKE_CURRENT_SOURCE_DIR}/src/annotations/private/coloradjustment.${stage}")
    set(out "${CMAKE_CURRENT_BINARY_DIR}/shaders/coloradjustment.${stage}.qsb")
    set(batchable)
    if(stage STREQUAL "vert")
        set(batchable -b)
    endif()
    add_custom_command(OUTPUT "${out}"
        COMMAND ${ECTOLENS_QSB} ${batchable} --glsl "100 es,120,150" --hlsl 50 --msl 12 -O -o "${out}" "${in}"
        DEPENDS "${in}"
        COMMENT "Compiling coloradjustment.${stage} with qsb"
        VERBATIM)
    list(APPEND shader_outputs "${out}")
endforeach()
qt_add_resources(EctolensImageEditor "kquickimageeditor_shaders"
    PREFIX "/qt/qml/org/kde/kquickimageeditor/private"
    BASE "${CMAKE_CURRENT_BINARY_DIR}/shaders"
    FILES ${shader_outputs})

target_include_directories(EctolensImageEditor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/annotations"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(EctolensImageEditor PUBLIC cxx_std_23)
kde_target_enable_exceptions(EctolensImageEditor PRIVATE)

if(BUILD_TESTING)
    add_subdirectory(autotests)
endif()
