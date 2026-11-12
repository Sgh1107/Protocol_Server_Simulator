# SPDX-License-Identifier: GPL-3.0-or-later
#
# UI：Widgets 主体（命令栏 / 设备栏 / 点表 / 报文）+ QML 局部（曲线）。
# 本模块只依赖 core 与适配器接口，不直接链接任何协议库。
# 视觉规格见 docs/design/ui-prototype-live.html。

add_library(protosim_ui STATIC
    include/protosim/ui/theme.h
    include/protosim/ui/signalmodel.h
    include/protosim/ui/framemodel.h
    include/protosim/ui/delegates.h
    include/protosim/ui/commandbar.h
    include/protosim/ui/signalrail.h
    include/protosim/ui/statusstrip.h
    include/protosim/ui/proportionalheaderview.h
    include/protosim/ui/mainwindow.h

    src/theme.cpp
    src/signalmodel.cpp
    src/framemodel.cpp
    src/delegates.cpp
    src/commandbar.cpp
    src/signalrail.cpp
    src/statusstrip.cpp
    src/proportionalheaderview.cpp
    src/mainwindow.cpp

    qml.qrc
    icons.qrc
)

target_include_directories(protosim_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(protosim_ui
    PUBLIC
        protosim_core
        protosim_adapters
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickWidgets
        protosim_compile_options
)
