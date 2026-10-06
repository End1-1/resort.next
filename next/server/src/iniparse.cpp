#include "iniparse.h"

#include <QDir>
#include <QFile>
#include <QStringDecoder>

namespace {

bool fail(QString *error, const QString &message)
{
    *error = message;
    return false;
}

bool takeKey(const QString &key, const QString &value, int lineNo, HotelIniValues *out, QString *error)
{
    if (key.compare(QLatin1String("listen"), Qt::CaseInsensitive) == 0) {
        out->hasListen = true;
        out->listen = value;
        return true;
    }
    if (key.compare(QLatin1String("mysql_host"), Qt::CaseInsensitive) == 0) {
        out->hasMysqlHost = true;
        out->mysqlHost = value;
        return true;
    }
    if (key.compare(QLatin1String("mysql_port"), Qt::CaseInsensitive) == 0) {
        out->hasMysqlPort = true;
        out->mysqlPort = value;
        return true;
    }
    if (key.compare(QLatin1String("mysql_schema"), Qt::CaseInsensitive) == 0) {
        out->hasMysqlSchema = true;
        out->mysqlSchema = value;
        return true;
    }
    if (key.compare(QLatin1String("mysql_user"), Qt::CaseInsensitive) == 0) {
        out->hasMysqlUser = true;
        out->mysqlUser = value;
        return true;
    }
    if (key.compare(QLatin1String("mysql_password"), Qt::CaseInsensitive) == 0) {
        out->hasMysqlPassword = true;
        out->mysqlPassword = value;
        return true;
    }
    if (key.compare(QLatin1String("mysql_ssl"), Qt::CaseInsensitive) == 0) {
        out->hasMysqlSsl = true;
        out->mysqlSsl = value;
        return true;
    }
    if (key.compare(QLatin1String("mysql_ssl_ca"), Qt::CaseInsensitive) == 0) {
        out->hasMysqlSslCa = true;
        out->mysqlSslCa = value;
        return true;
    }
    if (key.compare(QLatin1String("dsn"), Qt::CaseInsensitive) == 0) {
        out->hasDsn = true;
        out->dsn = value;
        return true;
    }
    if (key.compare(QLatin1String("ws_listen"), Qt::CaseInsensitive) == 0) {
        out->hasWsListen = true;
        out->wsListen = value;
        return true;
    }
    return fail(error, QStringLiteral("line %1: unknown key \"%2\"").arg(lineNo).arg(key));
}

bool takeLine(const QString &line, int lineNo, HotelIniValues *out, QString *error)
{
    if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
        return true;

    if (line.startsWith(QLatin1Char('['))) {
        if (!line.endsWith(QLatin1Char(']')) || line.size() < 3) {
            return fail(error, QStringLiteral("line %1 malformed").arg(lineNo));
        }
        const QString name = line.mid(1, line.size() - 2).trimmed();
        if (name.isEmpty())
            return fail(error, QStringLiteral("line %1 malformed").arg(lineNo));
        if (name.compare(QLatin1String("general"), Qt::CaseInsensitive) == 0
            || name.compare(QLatin1String("hotel-api"), Qt::CaseInsensitive) == 0) {
            return true;
        }
        return fail(error, QStringLiteral("line %1: unknown section [%2]").arg(lineNo).arg(name));
    }

    const int eq = line.indexOf(QLatin1Char('='));
    if (eq <= 0)
        return fail(error, QStringLiteral("line %1 malformed").arg(lineNo));

    const QString key = line.left(eq).trimmed();
    if (key.isEmpty())
        return fail(error, QStringLiteral("line %1 malformed").arg(lineNo));

    QString value = line.mid(eq + 1).trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
        value = value.mid(1, value.size() - 2);

    return takeKey(key, value, lineNo, out, error);
}

} // namespace

bool parseHotelIni(const QByteArray &raw, HotelIniValues *out, QString *error)
{
    *out = HotelIniValues{};

    QByteArray data = raw;
    if (data.size() >= 2
        && static_cast<unsigned char>(data.at(0)) == 0xFF
        && static_cast<unsigned char>(data.at(1)) == 0xFE) {
        return fail(error, QStringLiteral("file is UTF-16; save as UTF-8"));
    }
    if (data.size() >= 2
        && static_cast<unsigned char>(data.at(0)) == 0xFE
        && static_cast<unsigned char>(data.at(1)) == 0xFF) {
        return fail(error, QStringLiteral("file is UTF-16; save as UTF-8"));
    }
    if (data.size() >= 3
        && static_cast<unsigned char>(data.at(0)) == 0xEF
        && static_cast<unsigned char>(data.at(1)) == 0xBB
        && static_cast<unsigned char>(data.at(2)) == 0xBF) {
        data = data.mid(3);
    }
    if (data.contains('\0'))
        return fail(error, QStringLiteral("file is UTF-16; save as UTF-8"));

    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(data);
    if (decoder.hasError())
        return fail(error, QStringLiteral("file is not UTF-8; save as UTF-8"));

    qsizetype pos = 0;
    const qsizetype n = text.size();
    int lineNo = 0;
    while (pos < n) {
        qsizetype end = text.indexOf(QLatin1Char('\n'), pos);
        if (end < 0)
            end = n;
        QString line = text.mid(pos, end - pos);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        pos = end < n ? end + 1 : n;
        ++lineNo;
        if (!takeLine(line.trimmed(), lineNo, out, error))
            return false;
    }
    return true;
}

bool readHotelIniFile(const QString &path, HotelIniValues *out, QString *error)
{
    const QString native = QDir::toNativeSeparators(path);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("cannot open %1: %2").arg(native, file.errorString()));
    }

    QString detail;
    if (!parseHotelIni(file.readAll(), out, &detail)) {
        return fail(error, detail + QStringLiteral(": ") + native);
    }
    return true;
}
