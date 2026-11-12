# SPDX-License-Identifier: GPL-3.0-or-later
#
# sim-core：纯 Qt/C++ 逻辑层。
# 铁律：不链接任何协议库（SerialBus / 104 / MQTT / 61850）、不依赖 GUI。
# 协议适配器只通过 protosim::IProtocolAdapter 认识它。
# 源文件清单随任务推进逐个加入。

# 头文件必须列进来：AUTOMOC 只扫描 target 里的文件，
# include/ 与 src/ 不同目录时不会自动配对到 Q_OBJECT。
add_library(protosim_core STATIC
    include/protosim/version.h
    include/protosim/quality.h
    include/protosim/protocolkind.h
    include/protosim/signal.h
    include/protosim/byteorder.h
    include/protosim/snapshot.h
    include/protosim/databank.h
    include/protosim/result.h
    include/protosim/configbundle.h
    include/protosim/curve.h
    include/protosim/simulationengine.h
    include/protosim/iprotocoladapter.h
    include/protosim/telemetrylog.h
    include/protosim/eventbus.h
    include/protosim/writepipeline.h

    src/version.cpp
    src/quality.cpp
    src/protocolkind.cpp
    src/signal.cpp
    src/byteorder.cpp
    src/databank.cpp
    src/configbundle.cpp
    src/curve.cpp
    src/simulationengine.cpp
    src/adaptertypes.cpp
    src/telemetrylog.cpp
    src/eventbus.cpp
    src/writepipeline.cpp
)

target_include_directories(protosim_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 只认 QtCore。加一行 Qt6::SerialBus 就等于破功（AC-P0-01）。
target_link_libraries(protosim_core
    PUBLIC
        Qt6::Core
        protosim_compile_options
)

target_compile_definitions(protosim_core
    PUBLIC
        PROTOSIM_VERSION_STRING="${PROTOSIM_VERSION}"
)

set_target_properties(protosim_core PROPERTIES
    AUTOMOC ON
)
