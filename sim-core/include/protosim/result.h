// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace protosim {

/// 工程层错误码。协议层错误码（Modbus 0x01~0x04、104 COT）另走各适配器，
/// 两者不混用 —— 报文监视要能原样展示协议错误码（INTERFACES §2）。
enum class ErrorCode {
    Ok = 0,
    Io,             // 文件读写失败
    Parse,          // JSON 语法错
    Schema,         // 结构不符（缺字段、类型不对、取值越界）
    Semantic,       // 结构对但语义不成立（引用不存在、地址重叠……）
    PathTraversal,  // 配置包内的相对路径越出包目录
    Backup,         // 覆盖前备份失败
    Unsupported,    // 版本 / 能力不支持
    NotFound,
    InvalidArgument,
};

struct Error
{
    ErrorCode code = ErrorCode::Ok;
    QString message;
    QString detail;

    bool isOk() const { return code == ErrorCode::Ok; }

    static Error none() { return {}; }

    static Error make(ErrorCode code, const QString &message, const QString &detail = QString())
    {
        return Error{code, message, detail};
    }

    /// "message（detail）"，UI 直接显示，日志直接落盘。
    QString toString() const
    {
        if (detail.isEmpty())
            return message;
        return QStringLiteral("%1（%2）").arg(message, detail);
    }
};

/// 统一返回类型：不抛异常跨线程/跨进程（INTERFACES §1 第 5 条）。
template <typename T>
class Result
{
public:
    /// 默认构造 = "成功但值为空"，用于 ReadResult 这类聚合体的成员默认初始化。
    Result() = default;

    Result(const T &value) // NOLINT(google-explicit-constructor)：成功路径要能直接 return 值
        : m_value(value)
    {
    }

    static Result failure(const Error &error)
    {
        Result result;
        result.m_ok = false;
        result.m_error = error;
        return result;
    }

    static Result failure(ErrorCode code, const QString &message, const QString &detail = QString())
    {
        return failure(Error::make(code, message, detail));
    }

    bool ok() const { return m_ok; }
    const T &value() const { return m_value; }
    T &value() { return m_value; }
    const Error &error() const { return m_error; }

private:
    bool m_ok = true;
    T m_value{};
    Error m_error;
};

template <>
class Result<void>
{
public:
    Result() = default;

    static Result failure(const Error &error)
    {
        Result result;
        result.m_ok = false;
        result.m_error = error;
        return result;
    }

    static Result failure(ErrorCode code, const QString &message, const QString &detail = QString())
    {
        return failure(Error::make(code, message, detail));
    }

    bool ok() const { return m_ok; }
    const Error &error() const { return m_error; }

private:
    bool m_ok = true;
    Error m_error;
};

} // namespace protosim
