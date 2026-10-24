import QtQuick

Canvas {
    id: root
    required property var samples
    required property color lineColor
    required property color fillColor
    // Keep the small texture stable during live resizing; the scene graph scales
    // it to the item. Only data/theme changes need a new CPU paint/upload.
    canvasSize: Qt.size(240, 36)
    canvasWindow: Qt.rect(0, 0, 240, 36)
    onSamplesChanged: if (visible)
        requestPaint()
    onVisibleChanged: if (visible)
        requestPaint()
    onLineColorChanged: requestPaint()
    onFillColorChanged: requestPaint()
    onPaint: {
        const painter = getContext("2d");
        const plotWidth = canvasSize.width;
        const plotHeight = canvasSize.height;
        painter.clearRect(0, 0, plotWidth, plotHeight);
        const count = Math.min(120, samples.length);
        if (count < 2)
            return;
        let maximum = 1;
        for (let i = 0; i < count; ++i)
            maximum = Math.max(maximum, Number(samples[i]));
        function trace() {
            for (let i = 0; i < count; ++i) {
                const x = i * plotWidth / (count - 1);
                const y = plotHeight - 2 - Number(samples[i]) / maximum * (plotHeight - 4);
                if (i === 0)
                    painter.moveTo(x, y);
                else
                    painter.lineTo(x, y);
            }
        }
        painter.beginPath();
        trace();
        painter.lineTo(plotWidth, plotHeight);
        painter.lineTo(0, plotHeight);
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
