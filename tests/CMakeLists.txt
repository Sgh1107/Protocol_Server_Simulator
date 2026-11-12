# SPDX-License-Identifier: GPL-3.0-or-later
#
# 三层测试（P0_PLAN §3）：
#   L1 tests/unit      纯逻辑，无 GUI 无 socket
#   L2 tests/e2e       真实 socket、进程内假主站
#   L3                  真实主站（FieldLink）互验，不在 ctest 里，手工/录屏留证
# Qt 自带 Test 模块，不引第三方测试框架。

function(protosim_add_test TARGET)
    add_executable(${TARGET} ${ARGN})
    target_link_libraries(${TARGET} PRIVATE Qt6::Test protosim_compile_options)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
    set_tests_properties(${TARGET} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180
    )
endfunction()

add_subdirectory(unit)
add_subdirectory(e2e)
