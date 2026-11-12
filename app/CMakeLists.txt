# SPDX-License-Identifier: GPL-3.0-or-later

# 保留控制台子系统：--selftest / --version 的输出要能直接看见
# （绿色包内需要静默启动时再包一层 launcher）。
add_executable(ProtoSim
    main.cpp
    selftest.cpp
    selftest.h
)

target_link_libraries(ProtoSim
    PRIVATE
        protosim_ui
        protosim_adapters
        protosim_core
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Quick
        Qt6::QuickWidgets
        protosim_compile_options
)

# 开发期兜底：找不到绿色包内 config/ 时回源码目录取预设
target_compile_definitions(ProtoSim PRIVATE
    PROTOSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

set_target_properties(ProtoSim PROPERTIES
    WIN32_EXECUTABLE FALSE
)
