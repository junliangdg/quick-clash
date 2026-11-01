#include "domain/subscription_name.h"
#include <QRegularExpression>
#include <QStringDecoder>

namespace quickclash::domain {
namespace {
QString utf8(const QByteArray& bytes) {
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString result = decoder(bytes);
    return decoder.hasError() ? QString{} : result;
}
QString clean(const QString& text) {
    QString result;
    for (const auto character : text) {
        const auto category = character.category();
        if (category == QChar::Other_Control || category == QChar::Other_Format ||
            category == QChar::Separator_Line || category == QChar::Separator_Paragraph)
            continue;
        result += character;
    }
    return result.trimmed().left(80);
}
QString filename(QByteArray value, bool extended) {
    QString result;
    if (extended) {
        const auto first = value.indexOf('\'');
        const auto second = value.indexOf('\'', first + 1);
        if (first <= 0 || second < 0)
            return {};
        const auto charset = value.left(first).toLower();
        value = value.mid(second + 1);
        for (qsizetype index = 0; index < value.size(); ++index) {
            if (value[index] != '%')
                continue;
            const auto hex = [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            };
            if (index + 2 >= value.size() || !hex(value[index + 1]) || !hex(value[index + 2]))
                return {};
            index += 2;
        }
        value = QByteArray::fromPercentEncoding(value);
        if (charset == "utf-8")
            result = utf8(value);
        else if (charset == "iso-8859-1")
            result = QString::fromLatin1(value);
    } else {
        result = utf8(value);
    }
    result.replace(QChar(0x5c), QLatin1Char('/'));
    result = result.mid(result.lastIndexOf(QLatin1Char('/')) + 1).trimmed();
    for (const auto* suffix : {".yaml", ".yml"})
        if (result.endsWith(QLatin1String(suffix), Qt::CaseInsensitive)) {
            result.chop(static_cast<int>(qstrlen(suffix)));
            break;
        }
    result = clean(result);
    return result == QStringLiteral(".") || result == QStringLiteral("..") ? QString{} : result;
}
} // namespace
QString subscriptionName(const QByteArray& title, const QByteArray& disposition) {
    if (title.size() <= 4096) {
        auto bytes = title.trimmed();
        if (bytes.startsWith("base64:")) {
            const auto decoded = QByteArray::fromBase64Encoding(
                bytes.mid(7), QByteArray::AbortOnBase64DecodingErrors);
            bytes = decoded ? decoded.decoded : QByteArray{};
        }
        const auto name = clean(utf8(bytes));
        if (!name.isEmpty())
            return name;
    }
    if (disposition.size() > 4096)
        return {};
    static const QRegularExpression parameter(
        QStringLiteral(R"((?:^|;)\s*(filename\*?)\s*=\s*("(?:[^"\\]|\\.)*"|[^;]*))"),
        QRegularExpression::CaseInsensitiveOption);
    auto matches = parameter.globalMatch(QString::fromLatin1(disposition));
    QString regular, extended;
    while (matches.hasNext()) {
        const auto match = matches.next();
        auto value = match.captured(2).trimmed();
        if (value.startsWith(QLatin1Char('"'))) {
            if (!value.endsWith(QLatin1Char('"')) || value.size() < 2)
                continue;
            value = value.mid(1, value.size() - 2);
            value.replace(QRegularExpression(QStringLiteral(R"(\\(.))")), QStringLiteral("\\1"));
        }
        const bool encoded = match.captured(1).endsWith(QLatin1Char('*'));
        auto& name = encoded ? extended : regular;
        if (name.isEmpty())
            name = filename(value.toLatin1(), encoded);
    }
    return extended.isEmpty() ? regular : extended;
}
} // namespace quickclash::domain
