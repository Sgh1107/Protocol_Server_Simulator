// SPDX-License-Identifier: GPL-3.0-or-later
//
// 实时曲线。Y 轴固定在信号量程上（不是跟着数据缩放）——
// 这样"离上下限还有多远"才看得出来，这是这张图存在的意义。
//
// 数据由 C++ 每 tick 调 appendValue 推过来；颜色全部走 themeColors，
// 深浅主题切换时不需要改这里。

import QtQuick

Rectangle {
    id: root

    property real minValue: 0
    property real maxValue: 100
    property string labelName: ""
    property string labelUnit: ""
    property var values: []
    property int capacity: 300

    color: themeColors.background

    function appendValue(v) {
        var next = values.slice();
        next.push(Number(v));
        if (next.length > capacity)
            next.shift();
        values = next;
        canvas.requestPaint();
    }

    function resetValues() {
        values = [];
        canvas.requestPaint();
    }

    Canvas {
        id: canvas
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        function fmt(v) {
            // 刻度统一一位小数，避免 120 / 80.0 / 40.0 混排
            return Math.abs(v) >= 1000 ? Number(v).toFixed(0) : Number(v).toFixed(1);
        }

        function withAlpha(color, alpha) {
            // themeColors 给的是 #RRGGBB，拼成 rgba() 才能带透明度
            var r = parseInt(color.substr(1, 2), 16);
            var g = parseInt(color.substr(3, 2), 16);
            var b = parseInt(color.substr(5, 2), 16);
            return "rgba(" + r + "," + g + "," + b + "," + alpha + ")";
        }

        function drawXAxis(ctx, h, padL, iw, mono) {
            ctx.fillStyle = themeColors.subtleText;
            ctx.font = mono;
            var labels = ["-30s", "-20s", "-10s", "现在"];
            for (var i = 0; i < labels.length; ++i) {
                var x = padL + iw * i / 3;
                ctx.textAlign = i === 0 ? "left" : (i === 3 ? "right" : "center");
                ctx.fillText(labels[i], x, h - 8);
            }
        }

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();

            var w = width;
            var h = height;
            if (w <= 40 || h <= 40)
                return;

            var padL = 46;
            var padR = 14;
            var padT = 62;
            var padB = 24;
            var iw = w - padL - padR;
            var ih = h - padT - padB;
            if (iw <= 10 || ih <= 10)
                return;

            var lo = root.minValue;
            var hi = root.maxValue > root.minValue ? root.maxValue : root.minValue + 1;

            var mono = "10px 'Cascadia Mono', Consolas, monospace";
            var ui = "11px 'Microsoft YaHei UI', 'Segoe UI'";

            // ---- 标题与当前值 ----
            ctx.fillStyle = themeColors.subtleText;
            ctx.font = ui;
            ctx.textAlign = "left";
            ctx.fillText((root.labelName || "—") + " · 最近 30 s", padL, 16);

            var last = root.values.length > 0 ? root.values[root.values.length - 1] : NaN;
            var valueText = isNaN(last) ? "—" : Number(last).toFixed(3);

            ctx.fillStyle = themeColors.text;
            ctx.font = "600 22px 'Cascadia Mono', Consolas, monospace";
            ctx.fillText(valueText, padL, 46);
            var valueWidth = ctx.measureText(valueText).width;

            if (root.labelUnit !== "") {
                ctx.fillStyle = themeColors.subtleText;
                ctx.font = ui;
                ctx.fillText(root.labelUnit, padL + valueWidth + 6, 46);
            }

            ctx.fillStyle = themeColors.subtleText;
            ctx.font = mono;
            ctx.textAlign = "right";
            ctx.fillText("量程 " + fmt(lo) + "~" + fmt(hi), w - padR, 16);

            // ---- 网格与纵轴刻度 ----
            ctx.textAlign = "right";
            for (var g = 0; g <= 4; ++g) {
                var gy = padT + ih * g / 4;
                ctx.strokeStyle = themeColors.grid;
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(padL, gy + 0.5);
                ctx.lineTo(w - padR, gy + 0.5);
                ctx.stroke();

                ctx.fillStyle = themeColors.subtleText;
                ctx.font = mono;
                ctx.fillText(fmt(lo + (hi - lo) * (1 - g / 4)), padL - 8, gy + 3.5);
            }

            var n = root.values.length;
            if (n < 2) {
                drawXAxis(ctx, h, padL, iw, mono);
                return;
            }

            var pointX = function (i) {
                return padL + iw * (root.capacity - n + i) / (root.capacity - 1);
            };
            var pointY = function (v) {
                var ratio = (v - lo) / (hi - lo);
                return padT + ih - ih * Math.max(-0.1, Math.min(1.1, ratio));
            };

            // ---- 面积：折线垂到基线再闭合 ----
            var gradient = ctx.createLinearGradient(0, padT, 0, padT + ih);
            gradient.addColorStop(0, withAlpha(themeColors.accent, 0.22));
            gradient.addColorStop(1, withAlpha(themeColors.accent, 0));

            ctx.beginPath();
            ctx.moveTo(pointX(0), padT + ih);
            for (var i = 0; i < n; ++i)
                ctx.lineTo(pointX(i), pointY(root.values[i]));
            ctx.lineTo(pointX(n - 1), padT + ih);
            ctx.closePath();
            ctx.fillStyle = gradient;
            ctx.fill();

            // ---- 折线 ----
            ctx.beginPath();
            for (var j = 0; j < n; ++j) {
                if (j === 0)
                    ctx.moveTo(pointX(j), pointY(root.values[j]));
                else
                    ctx.lineTo(pointX(j), pointY(root.values[j]));
            }
            ctx.strokeStyle = themeColors.accent;
            ctx.lineWidth = 1.8;
            ctx.lineJoin = "round";
            ctx.lineCap = "round";
            ctx.stroke();

            // ---- 当前点 ----
            var cx = pointX(n - 1);
            var cy = pointY(root.values[n - 1]);
            ctx.beginPath();
            ctx.fillStyle = withAlpha(themeColors.accent, 0.18);
            ctx.arc(cx, cy, 7, 0, Math.PI * 2);
            ctx.fill();
            ctx.beginPath();
            ctx.fillStyle = themeColors.accent;
            ctx.arc(cx, cy, 3, 0, Math.PI * 2);
            ctx.fill();

            drawXAxis(ctx, h, padL, iw, mono);
        }
    }
}
