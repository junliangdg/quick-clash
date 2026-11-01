import QtQuick

QtObject {
    function bytes(value, rate): string {
        if (typeof value !== "number" || !isFinite(value) || value < 0)
            return "—";
        const units = rate ? ["B/s", "KiB/s", "MiB/s", "GiB/s", "TiB/s"] : ["B", "KiB", "MiB", "GiB", "TiB"];
        let index = 0;
        while (value >= 1024 && index < units.length - 1) { value /= 1024; ++index; }
        return value.toFixed(index === 0 || value >= 100 ? 0 : 1) + " " + units[index];
    }
    function time(value): string {
        const date = new Date(value || "");
        return isNaN(date.getTime()) ? (value || "—") : Qt.formatDateTime(date, "hh:mm:ss");
    }
    function target(entry): string {
        const m = entry.metadata || {};
        const host = m.host || m.destinationIP || "—";
        return host + (m.destinationPort ? ":" + m.destinationPort : "");
    }
    function process(entry): string {
        const m = entry.metadata || {};
        return (m.process || qsTr("Process unavailable")) + (m.network ? " · " + m.network.toUpperCase() : "");
    }
    function chain(entry): string { return (entry.chains || []).join(" → ") || "—"; }
}
