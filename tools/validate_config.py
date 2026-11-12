#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""配置包 JSON Schema 校验（开发期工具，不进运行时）。

职责边界：
  - 本脚本只做**结构**校验（config/schema/sim-config.schema.json，Draft 2020-12）。
  - 引用完整性、地址重叠、数据类型与寄存器数量匹配等**语义**校验在 C++ 应用层
    （sim-core/configbundle.cpp），因为运行时也必须拦住，不能只靠 CI。

用法：
    python tools/validate_config.py                          # 校验 config/examples 下全部
    python tools/validate_config.py config/examples/x.json    # 校验指定文件
    python tools/validate_config.py --schema other.json x.json

退出码：0 全部通过 / 1 有校验失败 / 2 环境缺依赖或文件缺失。
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SCHEMA = REPO_ROOT / "config" / "schema" / "sim-config.schema.json"
DEFAULT_TARGETS = sorted((REPO_ROOT / "config" / "examples").glob("*.json"))


def load_json(path: Path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except FileNotFoundError:
        print(f"[FAIL] 文件不存在：{path}", file=sys.stderr)
        return None
    except json.JSONDecodeError as exc:
        print(f"[FAIL] JSON 语法错误：{path}:{exc.lineno}:{exc.colno} {exc.msg}", file=sys.stderr)
        return None


def format_path(parts) -> str:
    out = ""
    for part in parts:
        if isinstance(part, int):
            out += f"[{part}]"
        else:
            out += f".{part}" if out else str(part)
    return out or "<root>"


def main() -> int:
    parser = argparse.ArgumentParser(description="校验 ProtoSim 配置包 JSON Schema")
    parser.add_argument("targets", nargs="*", type=Path, help="待校验的配置包，默认 config/examples/*.json")
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA, help="JSON Schema 路径")
    parser.add_argument("--quiet", action="store_true", help="只输出失败信息")
    args = parser.parse_args()

    try:
        from jsonschema import Draft202012Validator
    except ImportError:
        print(
            "[FAIL] 缺少 jsonschema 依赖。装一下开发期依赖：\n"
            "       python -m pip install -r tools/requirements.txt",
            file=sys.stderr,
        )
        return 2

    schema = load_json(args.schema)
    if schema is None:
        return 2

    targets = args.targets or DEFAULT_TARGETS
    if not targets:
        print("[FAIL] 没有找到任何待校验文件", file=sys.stderr)
        return 2

    try:
        validator = Draft202012Validator(schema)
    except Exception as exc:  # noqa: BLE001 - schema 自身坏了要原样报出来
        print(f"[FAIL] Schema 本身不合法：{exc}", file=sys.stderr)
        return 2

    failures = 0
    for target in targets:
        document = load_json(target)
        if document is None:
            failures += 1
            continue

        errors = sorted(validator.iter_errors(document), key=lambda e: list(e.absolute_path))
        if errors:
            failures += 1
            print(f"[FAIL] {target.relative_to(REPO_ROOT) if target.is_relative_to(REPO_ROOT) else target}")
            for error in errors[:20]:
                print(f"       {format_path(error.absolute_path)}: {error.message}")
            if len(errors) > 20:
                print(f"       ... 另有 {len(errors) - 20} 处问题")
        elif not args.quiet:
            print(f"[ OK ] {target.relative_to(REPO_ROOT) if target.is_relative_to(REPO_ROOT) else target}")

    if failures:
        print(f"\n{failures} 个文件未通过校验", file=sys.stderr)
        return 1

    if not args.quiet:
        print(f"\n全部通过（{len(targets)} 个文件）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
