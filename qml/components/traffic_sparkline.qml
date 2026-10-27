import QtQuick

// Dual-series traffic chart: upload (amber) and download (purple) share one
// coordinate system with a common maximum so both waves stay comparable.
// Samples advance left-to-right: the newest point enters from the right edge.
// The canvas follows the item size so the plot adapts to window resizing.
Canvas {
    id: root
    required property var uploadSamples
    required property var downloadSamples
    required property color uploadColor
    required property color downloadColor
    required property color gridColor
    required property real uploadFillOpacity
    required property real downloadFillOpacity
    canvasSize: Qt.size(root.width, root.height)
    canvasWindow: Qt.rect(0, 0, root.width, root.height)
    onUploadSamplesChanged: if (visible)
        requestPaint()
    onDownloadSamplesChanged: if (visible)
        requestPaint()
    onVisibleChanged: if (visible)
        requestPaint()
    onUploadColorChanged: requestPaint()
    onDownloadColorChanged: requestPaint()
    onGridColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const painter = getContext("2d");
        const plotWidth = root.width;
        const plotHeight = root.height;
        painter.clearRect(0, 0, plotWidth, plotHeight);
        if (plotWidth < 2 || plotHeight < 4)
            return;
        painter.strokeStyle = gridColor;
        painter.lineWidth = 1;
        for (let row = 1; row <= 3; ++row) {
            const y = Math.round((plotHeight - 4) * row / 3) + 0.5;
            painter.beginPath();
            painter.moveTo(0, y);
            painter.lineTo(plotWidth, y);
            painter.stroke();
        }
        const up = uploadSamples;
        const down = downloadSamples;
        const count = Math.min(up.length, down.length, 60);
        if (count < 2)
            return;
        let maximum = 1;
        for (let i = 0; i < count; ++i)
            maximum = Math.max(maximum, Number(up[i]) || 0, Number(down[i]) || 0);
        const step = plotWidth / (count - 1);
        // Keep 2 px head/tail room so the polyline never clips at the edges.
        const amplitude = plotHeight - 4;
        function trace(series) {
            painter.moveTo(0, plotHeight - 2 - (Number(series[0]) || 0) / maximum * amplitude);
            for (let i = 1; i < count; ++i) {
                painter.lineTo(i * step,
                              plotHeight - 2 - (Number(series[i]) || 0) / maximum * amplitude);
            }
        }
        function draw(series, color, fillOpacity) {
            painter.beginPath();
            trace(series);
            painter.lineTo(plotWidth, plotHeight);
            painter.lineTo(0, plotHeight);
            painter.closePath();
            painter.globalAlpha = fillOpacity;
            painter.fillStyle = color;
            painter.fill();
            painter.globalAlpha = 1;
            painter.beginPath();
            trace(series);
            painter.strokeStyle = color;
            painter.lineWidth = 1.5;
            painter.stroke();
        }
        draw(down, downloadColor, downloadFillOpacity);
        draw(up, uploadColor, uploadFillOpacity);
    }
}
