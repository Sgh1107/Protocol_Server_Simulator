#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""生成 ProtoSim 的图标资源。

一套 24×24 线性图标（笔画 1.6、圆角端点），外加一张应用图标。
产出三个地方：

  docs/design/icons/*.svg        设计源文件（可再编辑）
  ui/resources/icons/{dark,light}/*.png   给 Qt 用（20px / 40px @2x）
  ui/resources/app.ico           给 exe 用（多尺寸）

渲染走 Chrome 无头截图：把整组图标排成无间距网格，一次截一张再切，
比逐个调用快两个数量级（16 个图标 × 2 主题从 64 次降到 4 次）。

用法：
    python tools/make_icons.py            # 全量重建
    python tools/make_icons.py --sheet    # 只重建预览页
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
SVG_DIR = REPO_ROOT / "docs" / "design" / "icons"
RES_DIR = REPO_ROOT / "ui" / "resources"

CHROME_CANDIDATES = [
    Path(r"C:/Program Files/Google/Chrome/Application/chrome.exe"),
    Path(r"C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"),
]

DARK_INK = "#8a94a3"
LIGHT_INK = "#66738a"

# 24×24 网格。fill=None 表示纯描线；部分图标（播放/停止）用实心更好认。
ICONS: dict[str, str] = {
    "play": '<path d="M9.4 6.4 L18 12 L9.4 17.6 Z" fill="{INK}" stroke="none"/>',
    "pause": '<path d="M9.8 6.6 V17.4 M14.2 6.6 V17.4" stroke-width="2.4"/>',
    "step": '<path d="M8.2 6.6 L14.6 12 L8.2 17.4 Z" fill="{INK}" stroke="none"/>'
            '<path d="M16.6 6.6 V17.4" stroke-width="2.2"/>',
    "stop": '<rect x="7" y="7" width="10" height="10" rx="2" fill="{INK}" stroke="none"/>',
    "export": '<path d="M12 4.4 V14.6 M8.4 11 L12 14.6 L15.6 11 M4.8 18.6 H19.2"/>',
    "theme": '<circle cx="12" cy="12" r="7.4"/>'
             '<path d="M12 4.6 A7.4 7.4 0 0 0 12 19.4 Z" fill="{INK}" stroke="none"/>',
    "device": '<rect x="4.6" y="4.6" width="14.8" height="14.8" rx="2.6"/>'
              '<path d="M4.6 9.8 H19.4"/>'
              '<circle cx="8.2" cy="7.2" r="1" fill="{INK}" stroke="none"/>'
              '<circle cx="8.2" cy="12.4" r="1" fill="{INK}" stroke="none"/>',
    "signal": '<path d="M3.4 14.6 H7.4 L9.9 8.4 L13 17.2 L15.6 10.8 H20.6"/>',
    "link": '<circle cx="6.6" cy="12" r="2.6"/><circle cx="17.4" cy="12" r="2.6"/>'
            '<path d="M9.2 12 H14.8"/>',
    "fault": '<path d="M12 4.8 L20.6 19.2 H3.4 Z"/><path d="M12 10 V13.8"/>'
             '<circle cx="12" cy="16.6" r=".9" fill="{INK}" stroke="none"/>',
    "plus": '<path d="M12 6 V18 M6 12 H18"/>',
    "trash": '<path d="M5.4 7.4 H18.6 M9.4 7.4 V5.4 H14.6 V7.4"/>'
             '<path d="M7 7.4 L8 19 H16 L17 7.4"/>',
    "power": '<path d="M12 4.4 V11.2"/>'
             '<path d="M7.4 7.4 A6.9 6.9 0 1 0 16.6 7.4"/>',
    "database": '<ellipse cx="12" cy="7" rx="7" ry="2.8"/>'
                '<path d="M5 7 V17 C5 18.6 8.1 19.8 12 19.8 C15.9 19.8 19 18.6 19 17 V7"/>'
                '<path d="M5 12 C5 13.6 8.1 14.8 12 14.8 C15.9 14.8 19 13.6 19 12"/>',
    "refresh": '<path d="M19.4 12 A7.4 7.4 0 1 1 16.6 6.1"/>'
               '<path d="M16.9 3.4 V6.4 H13.9"/>',
    "settings": '<path d="M4.4 7.6 H19.6 M4.4 16.4 H19.6"/>'
                '<circle cx="9.4" cy="7.6" r="2.1" fill="#15181c"/>'
                '<circle cx="15.2" cy="16.4" r="2.1" fill="#15181c"/>',
}

APP_ICON = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="{SIZE}" height="{SIZE}">
  <rect width="256" height="256" rx="56" fill="#15181c"/>
  <rect x="1.5" y="1.5" width="253" height="253" rx="54.5" fill="none" stroke="#2b313b" stroke-width="3"/>
  <path d="M42 156 H94 V100 H162 V156 H214" fill="none" stroke="#4d8ff0" stroke-width="20"
        stroke-linecap="round" stroke-linejoin="round"/>
</svg>"""


def find_chrome() -> Path:
    for candidate in CHROME_CANDIDATES:
        if candidate.is_file():
            return candidate
    raise SystemExit("[FAIL] 找不到 Chrome/Edge，无法渲染")


def svg_markup(name: str, ink: str, size: int) -> str:
    body = ICONS[name].replace("{INK}", ink)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" '
            f'width="{size}" height="{size}" fill="none" stroke="{ink}" stroke-width="1.6" '
            f'stroke-linecap="round" stroke-linejoin="round">{body}</svg>')


def render(html_path: Path, png_path: Path, width: int, height: int, chrome: Path) -> None:
    cmd = [
        str(chrome), "--headless=new", "--disable-gpu", "--hide-scrollbars",
        "--default-background-color=00000000", "--force-device-scale-factor=1",
        f"--window-size={width},{height}", f"--screenshot={png_path}",
        html_path.as_uri(),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if not png_path.is_file():
        raise SystemExit(f"[FAIL] 渲染失败：{result.stderr[-400:]}")


def render_sheet(ink: str, size: int, out: Path, chrome: Path) -> None:
    """把整组图标排成无间距网格，一次截图。"""
    names = list(ICONS)
    cols = 8
    rows = (len(names) + cols - 1) // cols
    cells = "".join(f'<div>{svg_markup(n, ink, size)}</div>' for n in names)
    html = f"""<!doctype html><meta charset="utf-8">
<style>
  html,body{{margin:0;padding:0;background:transparent}}
  .grid{{display:grid;grid-template-columns:repeat({cols},{size}px);
         grid-auto-rows:{size}px;gap:0}}
  .grid>div{{width:{size}px;height:{size}px;line-height:0}}
  svg{{display:block}}
</style>
<div class="grid">{cells}</div>"""
    with tempfile.TemporaryDirectory() as tmp:
        html_path = Path(tmp) / "sheet.html"
        html_path.write_text(html, encoding="utf-8")
        render(html_path, out, cols * size, rows * size, chrome)


def slice_sheet(sheet: Path, size: int, out_dir: Path, suffix: str = "") -> None:
    image = Image.open(sheet).convert("RGBA")
    cols = 8
    out_dir.mkdir(parents=True, exist_ok=True)
    for index, name in enumerate(ICONS):
        col, row = index % cols, index // cols
        box = (col * size, row * size, (col + 1) * size, (row + 1) * size)
        image.crop(box).save(out_dir / f"{name}{suffix}.png")


def build_app_icon(chrome: Path) -> Path:
    with tempfile.TemporaryDirectory() as tmp:
        html_path = Path(tmp) / "app.html"
        html_path.write_text(
            "<!doctype html><meta charset=utf-8>"
            "<style>html,body{margin:0;padding:0;background:transparent}svg{display:block}</style>"
            + APP_ICON.replace("{SIZE}", "256"),
            encoding="utf-8",
        )
        png_path = Path(tmp) / "app.png"
        render(html_path, png_path, 256, 256, chrome)

        master = Image.open(png_path).convert("RGBA")
        RES_DIR.mkdir(parents=True, exist_ok=True)
        (REPO_ROOT / "docs" / "design" / "icons" / "app-256.png").parent.mkdir(
            parents=True, exist_ok=True)
        master.save(REPO_ROOT / "docs" / "design" / "icons" / "app-256.png")

        ico_path = RES_DIR / "app.ico"
        master.save(ico_path, format="ICO",
                    sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
        # 窗口图标用 256 PNG，QIcon 会自己缩
        master.save(RES_DIR / "app-256.png")
        return ico_path


def write_svg_sources() -> None:
    SVG_DIR.mkdir(parents=True, exist_ok=True)
    for name in ICONS:
        (SVG_DIR / f"{name}.svg").write_text(
            svg_markup(name, DARK_INK, 24) + "\n", encoding="utf-8")
    (SVG_DIR / "app.svg").write_text(APP_ICON.replace("{SIZE}", "256") + "\n", encoding="utf-8")


def write_preview(exe_icon: Path) -> None:
    names = list(ICONS)
    rows_dark = "".join(
        f'<div class="cell"><div class="ic">{svg_markup(n, DARK_INK, 24)}</div>'
        f'<span>{n}</span></div>' for n in names)
    rows_light = "".join(
        f'<div class="cell"><div class="ic">{svg_markup(n, LIGHT_INK, 24)}</div>'
        f'<span>{n}</span></div>' for n in names)

    html = f"""<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<title>ProtoSim 图标集</title>
<style>
  body{{margin:0;padding:40px 36px;background:#f2f3f5;color:#12151a;
        font-family:"Microsoft YaHei UI","Segoe UI",sans-serif}}
  h1{{font-size:20px;margin:0 0 6px}}
  p.sub{{color:#5d6675;font-size:13px;margin:0 0 28px;max-width:720px;line-height:1.7}}
  h2{{font-size:13px;margin:32px 0 12px;letter-spacing:.04em}}
  .board{{background:#15181c;border-radius:10px;padding:18px;display:flex;flex-wrap:wrap;gap:4px}}
  .board.light{{background:#fff;border:1px solid #dde3ec}}
  .cell{{width:76px;display:flex;flex-direction:column;align-items:center;gap:7px}}
  .ic{{width:36px;height:36px;display:flex;align-items:center;justify-content:center}}
  .cell span{{font-size:10px;font-family:Consolas,monospace;color:#8a94a3}}
  .board.light span{{color:#66738a}}
  .app{{display:flex;align-items:flex-end;gap:22px;margin-top:12px}}
  .app figure{{margin:0;text-align:center}}
  .app figcaption{{font-size:11px;color:#5d6675;margin-top:8px;font-family:Consolas,monospace}}
  .note{{margin-top:26px;font-size:12px;color:#5d6675;line-height:1.9}}
  code{{font-family:Consolas,monospace;background:#e8ebf0;padding:1px 5px;border-radius:3px}}
</style></head><body>
<h1>ProtoSim 图标集 v1</h1>
<p class="sub">24×24 网格、1.6px 描边、圆角端点。刻意不做填充色：图标只负责形状，
颜色由主题令牌给（深色 <code>#8a94a3</code> / 浅色 <code>#66738a</code>），换主题时整组跟着变。</p>

<h2>深色主题（24px 实尺寸）</h2>
<div class="board">{rows_dark}</div>

<h2>浅色主题（24px 实尺寸）</h2>
<div class="board light">{rows_light}</div>

<h2>应用图标</h2>
<div class="app">
  <figure><img src="icons/app-256.png" width="128" height="128"><figcaption>128</figcaption></figure>
  <figure><img src="icons/app-256.png" width="64" height="64"><figcaption>64</figcaption></figure>
  <figure><img src="icons/app-256.png" width="32" height="32"><figcaption>32</figcaption></figure>
  <figure><img src="icons/app-256.png" width="16" height="16"><figcaption>16</figcaption></figure>
</div>

<p class="note">
  应用图标是「一段方波 + 圆角深底板」：方波代表信号，底板保证在浅色任务栏上也立得住。
  16px 下笔画仍有约 1.2px，不会糊成一团。<br>
  已生成：<code>ui/resources/icons/{{dark,light}}/*.png</code>（20 / 40 @2x）、
  <code>ui/resources/app.ico</code>（16~256 七档）。
</p>
</body></html>"""
    (REPO_ROOT / "docs" / "design" / "icons.html").write_text(html, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="生成 ProtoSim 图标资源")
    parser.add_argument("--sheet", action="store_true", help="只重建预览页")
    args = parser.parse_args()

    chrome = find_chrome()

    if not args.sheet:
        write_svg_sources()
        print("[ .. ] 渲染图标网格")
        for theme, ink in (("dark", DARK_INK), ("light", LIGHT_INK)):
            for size, suffix in ((20, ""), (40, "@2x")):
                sheet = Path(tempfile.gettempdir()) / f"icons-{theme}-{size}.png"
                render_sheet(ink, size, sheet, chrome)
                out_dir = RES_DIR / "icons" / theme
                # 两档必须写到不同文件名：之前都写 {name}.png，40px 那轮把 20px 覆盖掉了
                slice_sheet(sheet, size, out_dir, suffix)
                print(f"[ OK ] {theme} {size}px -> {out_dir}")
        ico = build_app_icon(chrome)
        print(f"[ OK ] {ico}")

    write_preview(RES_DIR / "app.ico")
    print(f"[ OK ] {REPO_ROOT / 'docs/design/icons.html'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
