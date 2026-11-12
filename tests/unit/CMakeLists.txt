# SPDX-License-Identifier: GPL-3.0-or-later
# L1 纯逻辑单测：无 GUI、无 socket。

protosim_add_test(tst_version
    tst_version.cpp
)
target_link_libraries(tst_version PRIVATE protosim_core)

protosim_add_test(tst_byteorder
    tst_byteorder.cpp
)
target_link_libraries(tst_byteorder PRIVATE protosim_core)

protosim_add_test(tst_signal
    tst_signal.cpp
)
target_link_libraries(tst_signal PRIVATE protosim_core)

protosim_add_test(tst_databank
    tst_databank.cpp
)
target_link_libraries(tst_databank PRIVATE protosim_core)

protosim_add_test(tst_configbundle
    tst_configbundle.cpp
)
target_link_libraries(tst_configbundle PRIVATE protosim_core)
target_compile_definitions(tst_configbundle PRIVATE
    PROTOSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

protosim_add_test(tst_simulation
    tst_simulation.cpp
)
target_link_libraries(tst_simulation PRIVATE protosim_core)
target_compile_definitions(tst_simulation PRIVATE
    PROTOSIM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

protosim_add_test(tst_modbusframe
    tst_modbusframe.cpp
)
target_link_libraries(tst_modbusframe PRIVATE protosim_adapters)

protosim_add_test(tst_pipeline
    tst_pipeline.cpp
)
target_link_libraries(tst_pipeline PRIVATE protosim_core)
