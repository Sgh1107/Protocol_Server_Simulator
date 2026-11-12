# SPDX-License-Identifier: GPL-3.0-or-later
# L2 假主站 E2E：真实 socket、进程内。用例自查三个前提：
#   1. 主站不能用 waitForReadyRead —— 服务端在同一线程，会被堵死；
#   2. 每个用例用不同端口，避免上一个用例的监听端口还在 TIME_WAIT；
#   3. 断言聚焦"协议行为"而不是实现细节。

protosim_add_test(tst_modbus_e2e
    tst_modbus_e2e.cpp
)
target_link_libraries(tst_modbus_e2e PRIVATE protosim_adapters)
