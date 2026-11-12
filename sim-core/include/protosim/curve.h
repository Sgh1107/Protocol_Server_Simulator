// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "protosim/signal.h"

#include <QString>

#include <memory>

namespace protosim {

/// 数据源曲线接口。P0 只有常量 / 正弦 / 随机游走三种内置实现；
/// `QJSEngine` 脚本曲线、CSV 回放在 P1 以同一接口接入，引擎不改。
///
/// 约定：
///   - 返回的是**工程值**（已含 scale/offset 语义下的量纲），不回写原始寄存器值；
///   - 每个 tick 调用一次，`virtualTimeMs` 是仿真时钟（不是墙上时间）；
///   - 实现必须只依赖 (virtualTimeMs, deltaMs, 自身状态)，禁止读系统时间 / rand()，
///     否则同种子两跑就不可能逐帧一致（AC-P0-06）。
class Curve
{
public:
    virtual ~Curve() = default;

    /// 配置里的 behavior.type，用于 UI 展示与自检。
    virtual QString kind() const = 0;

    /// 推进一步并返回该 tick 的工程值。
    virtual double tick(qint64 virtualTimeMs, qint64 deltaMs) = 0;

    /// 回到初始状态（可复现的前提）。
    virtual void reset() = 0;
};

/// 按 signal.behavior 造曲线。不认识的 behavior.type 返回 nullptr 并写 *errorOut，
/// 由引擎记入"不支持的行为"清单，不静默当常量 0 处理。
///
/// `seed` 是场景种子；实现内部会再和 signal.id 混合，
/// 保证「增删一个信号不会改变其他信号的随机序列」。
std::unique_ptr<Curve> createCurve(const Signal &signal, quint64 seed, QString *errorOut);

/// 该 behavior.type 在 P0 是否已实现（UI/自检用）。
bool isBehaviorSupported(const QString &behaviorType);

} // namespace protosim
