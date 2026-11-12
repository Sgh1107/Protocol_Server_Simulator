# SPDX-License-Identifier: GPL-3.0-or-later
#
# 协议适配器。这是全仓唯一允许链接协议库的地方（Qt6::SerialBus）；
# sim-core 必须在它的上游保持干净（AC-P0-01）。

add_library(protosim_adapters STATIC
    include/protosim/adapters/modbusframe.h
    include/protosim/adapters/modbustap.h
    include/protosim/adapters/modbusadapter.h

    src/modbusframe.cpp
    src/modbustap.cpp
    src/modbusadapter.cpp
)

target_include_directories(protosim_adapters
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(protosim_adapters
    PUBLIC
        protosim_core
        Qt6::Core
        Qt6::Network
        Qt6::SerialBus
        protosim_compile_options
)
