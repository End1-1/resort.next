#include "mysqlclient.h"

#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <optional>

#if defined(Q_OS_LINUX)
#include <dlfcn.h>
#include <link.h>
#elif defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace {

constexpr qint64 kMaxPluginBytes = 64 * 1024 * 1024;

bool read16(const QByteArray &file, int off, quint16 *out)
{
    if (off < 0 || off + 2 > file.size())
        return false;
    const auto *p = reinterpret_cast<const uchar *>(file.constData() + off);
    *out = quint16(p[0]) | (quint16(p[1]) << 8);
    return true;
}

bool read32(const QByteArray &file, int off, quint32 *out)
{
    if (off < 0 || off + 4 > file.size())
        return false;
    const auto *p = reinterpret_cast<const uchar *>(file.constData() + off);
    *out = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    return true;
}

bool read64(const QByteArray &file, int off, quint64 *out)
{
    quint32 lo = 0;
    quint32 hi = 0;
    if (!read32(file, off, &lo) || !read32(file, off + 4, &hi))
        return false;
    *out = quint64(lo) | (quint64(hi) << 32);
    return true;
}

bool isMysqlClientName(const QString &name)
{
    return name.contains(QLatin1String("mysql"), Qt::CaseInsensitive)
        || name.contains(QLatin1String("mariadb"), Qt::CaseInsensitive);
}

QString cStringAt(const QByteArray &file, int off)
{
    if (off < 0 || off >= file.size())
        return {};
    const int end = file.indexOf('\0', off);
    if (end < 0)
        return {};
    return QString::fromUtf8(file.constData() + off, end - off);
}

QStringList elfClientLibraries(const QByteArray &file)
{
    if (file.size() < 64)
        return {};
    const auto *b = reinterpret_cast<const uchar *>(file.constData());
    if (b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F')
        return {};
    if (b[5] != 1)
        return {};
    const int cls = b[4];
    if (cls != 1 && cls != 2)
        return {};

    quint64 phoff64 = 0;
    quint16 phentsize = 0;
    quint16 phnum = 0;
    if (cls == 2) {
        if (!read64(file, 32, &phoff64) || !read16(file, 54, &phentsize) || !read16(file, 56, &phnum))
            return {};
    } else {
        quint32 phoff32 = 0;
        if (!read32(file, 28, &phoff32) || !read16(file, 42, &phentsize) || !read16(file, 44, &phnum))
            return {};
        phoff64 = phoff32;
    }
    if (phentsize < 32 || phnum == 0 || phnum > 128 || phoff64 > quint64(file.size()))
        return {};

    struct Seg {
        quint64 vaddr;
        quint64 offset;
        quint64 filesz;
    };
    QList<Seg> loads;
    quint64 dynVaddr = 0;
    quint64 dynSize = 0;
    bool haveDyn = false;
    for (quint16 i = 0; i < phnum; ++i) {
        const quint64 entry = phoff64 + quint64(i) * phentsize;
        if (entry > quint64(file.size()))
            return {};
        const int off = int(entry);
        quint32 type = 0;
        if (!read32(file, off, &type))
            return {};
        quint64 pOffset = 0;
        quint64 pVaddr = 0;
        quint64 pFilesz = 0;
        if (cls == 2) {
            if (!read64(file, off + 8, &pOffset) || !read64(file, off + 16, &pVaddr)
                || !read64(file, off + 32, &pFilesz))
                return {};
        } else {
            quint32 o = 0;
            quint32 v = 0;
            quint32 s = 0;
            if (!read32(file, off + 4, &o) || !read32(file, off + 8, &v) || !read32(file, off + 16, &s))
                return {};
            pOffset = o;
            pVaddr = v;
            pFilesz = s;
        }
        if (type == 1)
            loads.append(Seg{pVaddr, pOffset, pFilesz});
        if (type == 2) {
            dynVaddr = pVaddr;
            dynSize = pFilesz;
            haveDyn = true;
        }
    }
    if (!haveDyn || dynSize == 0 || dynSize > (1u << 20))
        return {};

    const auto toOffset = [&](quint64 vaddr) -> qint64 {
        for (const Seg &seg : loads) {
            if (vaddr >= seg.vaddr && vaddr - seg.vaddr < seg.filesz)
                return qint64(seg.offset + (vaddr - seg.vaddr));
        }
        return -1;
    };

    const qint64 dynOff = toOffset(dynVaddr);
    if (dynOff < 0)
        return {};
    const int stride = cls == 2 ? 16 : 8;
    quint64 strtabV = 0;
    QList<quint64> needed;
    for (quint64 i = 0; i + quint64(stride) <= dynSize; i += quint64(stride)) {
        const int ent = int(dynOff + qint64(i));
        quint64 tag = 0;
        quint64 val = 0;
        if (cls == 2) {
            if (!read64(file, ent, &tag) || !read64(file, ent + 8, &val))
                break;
        } else {
            quint32 tag32 = 0;
            quint32 val32 = 0;
            if (!read32(file, ent, &tag32) || !read32(file, ent + 4, &val32))
                break;
            tag = tag32;
            val = val32;
        }
        if (tag == 0)
            break;
        if (tag == 5)
            strtabV = val;
        if (tag == 1)
            needed.append(val);
    }
    if (strtabV == 0 || needed.isEmpty())
        return {};
    const qint64 strOff = toOffset(strtabV);
    if (strOff < 0)
        return {};

    QStringList names;
    for (quint64 rel : needed) {
        if (rel > quint64(file.size()))
            continue;
        const QString name = cStringAt(file, int(strOff + qint64(rel)));
        if (isMysqlClientName(name))
            names.append(name);
    }
    return names;
}

QStringList peClientLibraries(const QByteArray &file)
{
    quint16 magic = 0;
    if (!read16(file, 0, &magic) || magic != 0x5A4D)
        return {};
    quint32 lfanew = 0;
    if (!read32(file, 0x3C, &lfanew))
        return {};
    quint32 pe = 0;
    if (!read32(file, int(lfanew), &pe) || pe != 0x00004550)
        return {};
    const int coff = int(lfanew) + 4;
    quint16 numSections = 0;
    quint16 optSize = 0;
    if (!read16(file, coff + 2, &numSections) || !read16(file, coff + 16, &optSize))
        return {};
    if (numSections == 0 || numSections > 96 || optSize < 96)
        return {};
    const int opt = coff + 20;
    if (opt + int(optSize) > file.size())
        return {};
    quint16 optMagic = 0;
    if (!read16(file, opt, &optMagic))
        return {};

    int ddOff = 0;
    quint32 numRva = 0;
    if (optMagic == 0x20b) {
        if (optSize < 240 || !read32(file, opt + 108, &numRva))
            return {};
        ddOff = opt + 112;
    } else if (optMagic == 0x10b) {
        if (optSize < 224 || !read32(file, opt + 92, &numRva))
            return {};
        ddOff = opt + 96;
    } else {
        return {};
    }
    if (numRva < 2 || ddOff + 16 > file.size())
        return {};
    quint32 importRva = 0;
    quint32 importSize = 0;
    if (!read32(file, ddOff + 8, &importRva) || !read32(file, ddOff + 12, &importSize))
        return {};
    if (importRva == 0 || importSize < 20)
        return {};

    const int secOff = opt + int(optSize);
    const auto rvaToOff = [&](quint32 rva) -> qint64 {
        for (quint16 i = 0; i < numSections; ++i) {
            const int s = secOff + int(i) * 40;
            if (s + 40 > file.size())
                return -1;
            quint32 vsize = 0;
            quint32 va = 0;
            quint32 rawSize = 0;
            quint32 rawPtr = 0;
            if (!read32(file, s + 8, &vsize) || !read32(file, s + 12, &va) || !read32(file, s + 16, &rawSize)
                || !read32(file, s + 20, &rawPtr))
                return -1;
            const quint32 span = qMax(vsize, rawSize);
            if (span == 0 || rva < va || rva - va >= span || rawSize == 0)
                continue;
            const quint32 delta = rva - va;
            if (delta >= rawSize)
                return -1;
            return qint64(rawPtr) + delta;
        }
        return -1;
    };

    const qint64 imp = rvaToOff(importRva);
    if (imp < 0)
        return {};
    QStringList names;
    for (int n = 0; n < 64; ++n) {
        const int ent = int(imp) + n * 20;
        quint32 orig = 0;
        quint32 nameRva = 0;
        quint32 thunk = 0;
        if (!read32(file, ent, &orig) || !read32(file, ent + 12, &nameRva) || !read32(file, ent + 16, &thunk))
            break;
        if (orig == 0 && nameRva == 0 && thunk == 0)
            break;
        if (nameRva == 0)
            continue;
        const QString name = cStringAt(file, int(rvaToOff(nameRva)));
        if (isMysqlClientName(name))
            names.append(name);
    }
    return names;
}

bool libraryNameIsMariaDb(const QString &name)
{
    return name.contains(QLatin1String("mariadb"), Qt::CaseInsensitive);
}

std::optional<QString> clientInfoFromLoadedLibrary(const QString &libraryName)
{
    const QString base = QFileInfo(libraryName).fileName();
    const QString file = base.isEmpty() ? libraryName : base;
#if defined(Q_OS_LINUX)
    const QByteArray utf8 = file.toUtf8();
    void *handle = dlopen(utf8.constData(), RTLD_LAZY | RTLD_NOLOAD);
    bool opened = false;
    if (!handle) {
        handle = dlopen(utf8.constData(), RTLD_LAZY | RTLD_LOCAL);
        opened = handle != nullptr;
    }
    if (!handle)
        return std::nullopt;
    using Fn = const char *(*)();
    auto *fn = reinterpret_cast<Fn>(dlsym(handle, "mysql_get_client_info"));
    std::optional<QString> info;
    if (fn) {
        if (const char *text = fn())
            info = QString::fromUtf8(text);
    }
    if (opened)
        dlclose(handle);
    return info;
#elif defined(Q_OS_WIN)
    const std::wstring wide = file.toStdWString();
    HMODULE mod = GetModuleHandleW(wide.c_str());
    bool loaded = false;
    if (!mod) {
        mod = LoadLibraryW(wide.c_str());
        loaded = mod != nullptr;
    }
    if (!mod)
        return std::nullopt;
    using Fn = const char *(__stdcall *)();
    auto *fn = reinterpret_cast<Fn>(GetProcAddress(mod, "mysql_get_client_info"));
    std::optional<QString> info;
    if (fn) {
        if (const char *text = fn())
            info = QString::fromUtf8(text);
    }
    if (loaded)
        FreeLibrary(mod);
    return info;
#else
    Q_UNUSED(file);
    return std::nullopt;
#endif
}

std::optional<MysqlClientKind> kindFromPluginFile(const QString &path)
{
    const QStringList libs = mysqlClientLibrariesInPlugin(path);
    if (libs.isEmpty())
        return std::nullopt;
    for (const QString &lib : libs) {
        if (libraryNameIsMariaDb(lib))
            return MysqlClientKind::MariaDb;
    }
    for (const QString &lib : libs) {
        const std::optional<QString> info = clientInfoFromLoadedLibrary(lib);
        if (info.has_value())
            return mysqlClientKindFromInfo(*info);
    }
    for (const QString &lib : libs) {
        if (lib.contains(QLatin1String("mysqlclient"), Qt::CaseInsensitive))
            return MysqlClientKind::LibMySql;
    }
    return std::nullopt;
}

} // namespace

QStringList mysqlClientLibrariesInPlugin(const QString &pluginPath)
{
    QFile file(pluginPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    if (file.size() <= 0 || file.size() > kMaxPluginBytes)
        return {};
    const QByteArray bytes = file.readAll();
    const QStringList elf = elfClientLibraries(bytes);
    if (!elf.isEmpty())
        return elf;
    return peClientLibraries(bytes);
}

QString loadedQmysqlPluginPath()
{
#if defined(Q_OS_LINUX)
    QString found;
    dl_iterate_phdr(
        [](struct dl_phdr_info *info, size_t, void *data) -> int {
            auto *out = static_cast<QString *>(data);
            if (!out->isEmpty() || info->dlpi_name == nullptr || info->dlpi_name[0] == '\0')
                return 0;
            const QString name = QString::fromUtf8(info->dlpi_name);
            if (name.contains(QLatin1String("qsqlmysql"), Qt::CaseInsensitive))
                *out = name;
            return 0;
        },
        &found);
    return found;
#elif defined(Q_OS_WIN)
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return {};
    MODULEENTRY32W module;
    module.dwSize = sizeof(module);
    QString found;
    if (Module32FirstW(snap, &module)) {
        do {
            const QString name = QString::fromWCharArray(module.szModule);
            if (name.contains(QLatin1String("qsqlmysql"), Qt::CaseInsensitive)) {
                found = QString::fromWCharArray(module.szExePath);
                break;
            }
        } while (Module32NextW(snap, &module));
    }
    CloseHandle(snap);
    return found;
#else
    return {};
#endif
}

MysqlClientKind detectedMysqlClientKind()
{
    static QMutex mutex;
    static bool ready = false;
    static MysqlClientKind cached = MysqlClientKind::LibMySql;
    {
        const QMutexLocker lock(&mutex);
        if (ready)
            return cached;
    }

    const QString path = loadedQmysqlPluginPath();
    const std::optional<MysqlClientKind> found = path.isEmpty() ? std::nullopt : kindFromPluginFile(path);

    const QMutexLocker lock(&mutex);
    if (found.has_value() && !ready) {
        cached = *found;
        ready = true;
    }
    return ready ? cached : MysqlClientKind::LibMySql;
}
