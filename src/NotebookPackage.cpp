#include "NotebookPackage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QDebug>

// miniz - 与上游 SpeedyNote 相同的 ZIP 库（MIT）
#include "miniz.h"

namespace SyncMod {

QList<NotebookPackage::Info> NotebookPackage::scanNotebooks(const QString& rootDir)
{
    QList<Info> result;
    QDir dir(rootDir);
    if (!dir.exists())
        return result;

    for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!fi.fileName().endsWith(".snb", Qt::CaseInsensitive))
            continue;
        Info info;
        info.bundlePath = fi.absoluteFilePath();
        info.name = fi.completeBaseName();   // 去掉 .snb
        info.modified = fi.lastModified();
        result.append(info);
    }
    return result;
}

QByteArray NotebookPackage::packBundle(const QString& snbDir,
                                       bool* ok, QString* errorMessage)
{
    QByteArray out;
    if (ok) *ok = false;

    QDir bundleDir(snbDir);
    if (!bundleDir.exists()) {
        if (errorMessage) *errorMessage = QStringLiteral("笔记本目录不存在: %1").arg(snbDir);
        return out;
    }

    // 用内存 ZIP writer
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法初始化 ZIP writer");
        return out;
    }

    QString notebookName = bundleDir.dirName();
    if (!notebookName.endsWith(".snb"))
        notebookName += ".snb";

    bool addFailed = false;
    QDirIterator it(snbDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFile file(it.filePath());
        if (!file.open(QIODevice::ReadOnly)) {
            addFailed = true;
            break;
        }
        QByteArray data = file.readAll();
        file.close();

        QString rel = bundleDir.relativeFilePath(it.filePath());
        QString zipEntry = notebookName + "/" + rel;
        QByteArray entryUtf8 = zipEntry.toUtf8();
        if (!mz_zip_writer_add_mem(&zip, entryUtf8.constData(),
                                   data.constData(), data.size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            addFailed = true;
            break;
        }
    }

    if (addFailed) {
        mz_zip_writer_end(&zip);
        if (errorMessage) *errorMessage = QStringLiteral("打包笔记本失败: %1").arg(notebookName);
        return out;
    }

    void* heapBuf = nullptr;
    size_t heapSize = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &heapBuf, &heapSize)) {
        mz_zip_writer_end(&zip);
        if (errorMessage) *errorMessage = QStringLiteral("无法完成 ZIP 打包");
        return out;
    }
    out = QByteArray(static_cast<const char*>(heapBuf), static_cast<int>(heapSize));
    mz_free(heapBuf);
    mz_zip_writer_end(&zip);

    if (ok) *ok = true;
    return out;
}

QString NotebookPackage::unpackToTempFile(const QByteArray& snbxData,
                                          const QString& remoteName,
                                          bool* ok, QString* errorMessage)
{
    if (ok) *ok = false;

    if (snbxData.isEmpty() || remoteName.isEmpty()) {
        if (errorMessage) *errorMessage = QStringLiteral("数据或文件名为空");
        return QString();
    }

    // 确保扩展名是 .snbx
    QString safeName = remoteName;
    if (!safeName.endsWith(".snbx", Qt::CaseInsensitive))
        safeName += ".snbx";
    // 去掉路径分隔符，防止路径穿越
    safeName = QFileInfo(safeName).fileName();

    QString path = QDir::temp().filePath("speedynote-sync-" + safeName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法写临时文件: %1").arg(file.errorString());
        return QString();
    }
    file.write(snbxData);
    file.close();

    if (ok) *ok = true;
    return path;
}

} // namespace SyncMod
