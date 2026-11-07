# SPDX-FileCopyrightText: 2025 Carl Schwan <carl@carlschwan.eu>
# SPDX-FileCopyrightText: 2026 Ectolens contributors
# SPDX-License-Identifier: BSD-2-Clause
#
# Upstream's stack blur and color adjustment tests, run on Ectolens's copy. The Highway
# benchmark is left out with Highway itself.

find_package(Qt6 CONFIG REQUIRED Test)

add_executable(stackblurtest_bin
    stackblurtest_helpers.h
    stackblurtest.cpp
    ../src/annotations/stackblur.cpp
)
target_include_directories(stackblurtest_bin PRIVATE ../src/annotations)
target_link_libraries(stackblurtest_bin PRIVATE Qt::Test Qt::Gui Qt::Concurrent)
target_compile_features(stackblurtest_bin PRIVATE cxx_std_23)
ecm_mark_as_test(stackblurtest_bin)
add_test(NAME stackblurtest COMMAND stackblurtest_bin)
set_tests_properties(stackblurtest PROPERTIES TIMEOUT 60)

add_executable(coloradjustmenttest_bin
    stackblurtest_helpers.h
    coloradjustmenttest.cpp
    ../src/annotations/coloradjustment.cpp
)
target_include_directories(coloradjustmenttest_bin PRIVATE ../src/annotations)
target_link_libraries(coloradjustmenttest_bin PRIVATE Qt::Test Qt::Gui)
target_compile_features(coloradjustmenttest_bin PRIVATE cxx_std_23)
ecm_mark_as_test(coloradjustmenttest_bin)
add_test(NAME coloradjustmenttest COMMAND coloradjustmenttest_bin)
set_tests_properties(coloradjustmenttest PROPERTIES TIMEOUT 10)
