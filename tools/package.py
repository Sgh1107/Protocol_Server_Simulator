#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""打绿色 zip：windeployqt + 配置包 + 一份能照着跑的 README。

不改系统、不装依赖，产物只有 dist/ 下的一个 zip（AC-P0-18）。

用法：
    python tools/package.py                         # 自动找 windeployqt
    python tools/package.py --qt D:/Qt/6.6.3/msvc2019_64 --config Release
    python tools/package.py --skip-deploy           # 只拷 exe 与配置，调试用

注意：
  - `--qmldir ui/qml` 不能省，否则运行时缺 QML 模块；
  - Qt 的 DLL 是 LGPLv3 档，分发时必须随包给许可文本（本脚本会带 LGPL 说明）。
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_QT = Path(r"D:/programming/Qt/6.6.3/msvc2019_64")


def find_windeployqt(qt_dir: Path | None) -> Path | None:
    candidates = []
    if qt_dir:
        candidates.append(qt_dir / "bin" / "windeployqt.exe")
    for entry in os.environ.get("PATH", "").split(os.pathsep):
        if entry:
            candidates.append(Path(entry) / "windeployqt.exe")
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def run_deploy(windeploy: Path, exe: Path, qml_dir: Path) -> None:
    if not qml_dir.is_dir():
        raise SystemExit(f"[FAIL] --qmldir 目录不存在：{qml_dir}")

    cmd = [
        str(windeploy),
        "--release",
        "--no-translations",
        "--no-system-d3d-compiler",
        "--qmldir",
        str(qml_dir),
        str(exe),
    ]
    print("[ .. ] " + " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        raise SystemExit("[FAIL] windeployqt 失败")
    print("[ OK ] windeployqt 完成")


def main() -> int:
    parser = argparse.ArgumentParser(description="打包 ProtoSim 绿色 zip")
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / "build")
    parser.add_argument("--config", default="Release")
    parser.add_argument("--qt", type=Path, default=None)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "dist")
    parser.add_argument("--skip-deploy", action="store_true")
    args = parser.parse_args()

    exe = args.build_dir / "bin" / "ProtoSim.exe"
    if not exe.is_file():
        raise SystemExit(f"[FAIL] 找不到 {exe}，先构建一次")

    qt_dir = args.qt or (DEFAULT_QT if DEFAULT_QT.is_dir() else None)

    stage = args.out / "ProtoSim"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True, exist_ok=True)

    shutil.copy2(exe, stage / "ProtoSim.exe")

    if not args.skip_deploy:
        windeploy = find_windeployqt(qt_dir)
        if windeploy is None:
            raise SystemExit("[FAIL] 找不到 windeployqt，用 --qt 指定 Qt 目录")
        run_deploy(windeploy, stage / "ProtoSim.exe", REPO_ROOT / "ui" / "qml")

    # 配置包必须整目录带走：绿色包要在任意机器上解压即跑
    config_src = REPO_ROOT / "config"
    if config_src.is_dir():
        shutil.copytree(config_src, stage / "config")

    # 许可证与说明
    for name in ("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md"):
        source = REPO_ROOT / name
        if source.is_file():
            shutil.copy2(source, stage / name)

    quick_start = stage / "快速开始.txt"
    quick_start.write_text(
        "ProtoSim 绿色包\n"
        "================\n\n"
        "1. 双击 ProtoSim.exe 即可启动，默认场景是 FieldLink 联调预设；\n"
        "   模拟器起来后 Modbus TCP 监听在 127.0.0.1:1502。\n"
        "2. 用 FieldLink 连 127.0.0.1:1502，读 HR[0..1] 应看到 15~35 之间按 10s 周期变化的温度。\n"
        "3. FC16 写 HR[0..1]（float32），再 FC03 回读，值应一致。\n\n"
        "命令行：\n"
        "  ProtoSim.exe --selftest          跑 9 项内置自检\n"
        "  ProtoSim.exe --theme light       浅色主题启动\n"
        "  ProtoSim.exe --config <路径>     指定场景配置包\n\n"
        "首次运行 Windows Defender 可能弹入站规则提示，需允许专用网络。\n"
        "若 QML 渲染异常（远程桌面 / 老虚拟机），设环境变量 QSG_RHI_BACKEND=software。\n",
        encoding="utf-8",
    )

    version = subprocess.run(
        [str(stage / "ProtoSim.exe"), "--version"], capture_output=True, text=True
    ).stdout.strip().splitlines()
    version_text = (version[0] if version else "unknown").replace(" ", "-")

    zip_path = args.out / f"ProtoSim-{version_text}-win64.zip"
    if zip_path.exists():
        zip_path.unlink()

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                archive.write(path, Path("ProtoSim") / path.relative_to(stage))

    total = sum(p.stat().st_size for p in stage.rglob("*") if p.is_file())
    print(f"[ OK ] {zip_path}  ({total / 1024 / 1024:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
