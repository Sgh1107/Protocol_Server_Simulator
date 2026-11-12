// SPDX-License-Identifier: GPL-3.0-or-later
//
// 曲线局部视图。QML 只负责画，数据由 C++ 每 tick 调 appendValue 推过来。
// 颜色全部走 themeColors（theme.h 的 ThemeBridge），深浅主题切换不需要改 QML。

import QtQuick

Rectangle {
    id: root

    property var values: []
    property int maxPoints: 300
    property string label: ""
    property string valueText: ""

    color: themeColors.background

    function appendValue(v) {
        var next = values.slice();
        next.push(Number(v));
        if (next.length > maxPoints)
            next.shift();
        values = next;
        valueText = Number(v).toFixed(3);
        canvas.requestPaint();
    }

    function resetValues() {
        values = [];
        valueText = "";
        canvas.requestPaint();
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        anchors.margins: 12

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();

            var w = width;
            var h = height;
            if (w <= 1 || h <= 1)
                return;

            ctx.strokeStyle = themeColors.grid;
            ctx.lineWidth = 1;
            for (var g = 0; g <= 4; ++g) {
                var gy = Math.round(h * g / 4) + 0.5;
                ctx.beginPath();
                ctx.moveTo(0, gy);
                ctx.lineTo(w, gy);
                ctx.stroke();
            }

            if (root.values.length < 2)
                return;

            var min = Math.min.apply(null, root.values);
            var max = Math.max.apply(null, root.values);
            if (max - min < 1e-9)
                max = min + 1;

            var pad = (max - min) * 0.12;
            min -= pad;
            max += pad;

            ctx.strokeStyle = themeColors.accent;
            ctx.lineWidth = 1.6;
            ctx.beginPath();
            for (var i = 0; i < root.values.length; ++i) {
                var x = w * i / Math.max(1, root.maxPoints - 1);
                var ratio = (root.values[i] - min) / (max - min);
                var y = h - ratio * h;
                if (i === 0)
                    ctx.moveTo(x, y);
                else
                    ctx.lineTo(x, y);
            }
            ctx.stroke();
        }
    }

    Column {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 2

        Text {
            text: root.label
            color: themeColors.subtleText
            font.pixelSize: 11
        }
        Text {
            text: root.valueText
            color: themeColors.text
            font.pixelSize: 20
            font.bold: true
        }
    }
}
