import QtQuick

Canvas {
    id: root
    required property var samples
    required property color lineColor
    required property color fillColor
    onSamplesChanged: if (visible)
        requestPaint()
    onVisibleChanged: if (visible)
        requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onLineColorChanged: requestPaint()
    onFillColorChanged: requestPaint()
    onPaint: {
        const painter = getContext("2d");
        painter.clearRect(0, 0, width, height);
        const count = Math.min(120, samples.length);
        if (count < 2 || width <= 0 || height <= 4)
            return;
        let maximum = 1;
        for (let i = 0; i < count; ++i)
            maximum = Math.max(maximum, Number(samples[i]));
        function trace() {
            for (let i = 0; i < count; ++i) {
                const x = i * width / (count - 1);
                const y = height - 2 - Number(samples[i]) / maximum * (height - 4);
                if (i === 0)
                    painter.moveTo(x, y);
                else
                    painter.lineTo(x, y);
            }
        }
        painter.beginPath();
        trace();
        painter.lineTo(width, height);
        painter.lineTo(0, height);
        painter.closePath();
        painter.fillStyle = fillColor;
        painter.fill();
        painter.beginPath();
        trace();
        painter.strokeStyle = lineColor;
        painter.lineWidth = 1.5;
        painter.stroke();
    }
}
