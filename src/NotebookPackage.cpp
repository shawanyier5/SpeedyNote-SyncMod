#include "NotebookPackage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
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

// 从 document.json 里收集"外部 PDF"（不在 .snb 文件夹内的，才会被漏掉）。
// .snb 内部的 PDF 会被 QDirIterator 直接打包，无需额外处理。
// 兼容三种写法：legacy pdf_path / pdf_relative_path / 多源 pdf_sources[]。
static QList<QString> collectExternalPdfs(const QString& snbDir,
                                          const QJsonObject& root)
{
    const QDir bundle(snbDir);
    const QString bundleAbs = bundle.absolutePath();
    QList<QString> result;
    QSet<QString> seen;

    auto add = [&](const QString& raw) {
        if (raw.isEmpty())
            return;
        QString resolved = raw;
        if (!QFileInfo(resolved).isAbsolute())
            resolved = bundle.absoluteFilePath(resolved);
        resolved = QFileInfo(resolved).canonicalFilePath();
        if (resolved.isEmpty())
            return;
        // 已经在 .snb 内部，QDirIterator 会打包它
        if (resolved.startsWith(bundleAbs + QStringLiteral("/")))
            return;
        if (!QFileInfo(resolved).isFile())
            return;
        if (!resolved.endsWith(".pdf", Qt::CaseInsensitive))
            return;
        if (seen.contains(resolved))
            return;
        seen.insert(resolved);
        result.append(resolved);
    };

    if (root.value(QStringLiteral("pdf_sources")).isArray()) {
        const QJsonArray sources = root.value(QStringLiteral("pdf_sources")).toArray();
        for (const QJsonValue& v : sources) {
            if (!v.isObject())
                continue;
            const QJsonObject src = v.toObject();
            add(src.value(QStringLiteral("path")).toString());
            add(src.value(QStringLiteral("relative_path")).toString());
        }
    }
    add(root.value(QStringLiteral("pdf_path")).toString());
    add(root.value(QStringLiteral("pdf_relative_path")).toString());

    return result;
}

// 把一个绝对路径改写为包内路径（若它在待嵌入的 PDF 列表里）
static void rewritePdfPath(QJsonObject& obj,
                           const QString& key,
                           const QString& snbDir,
                           const QMap<QString, QString>& pdfZipName)
{
    if (!obj.contains(key))
        return;
    const QString raw = obj.value(key).toString();
    if (raw.isEmpty())
        return;
    QString resolved = raw;
    if (!QFileInfo(resolved).isAbsolute())
        resolved = QDir(snbDir).absoluteFilePath(raw);
    resolved = QFileInfo(resolved).canonicalFilePath();
    if (pdfZipName.contains(resolved))
        obj[key] = QStringLiteral("../embedded/") + pdfZipName.value(resolved);
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

    QString notebookName = bundleDir.dirName();
    if (!notebookName.endsWith(".snb"))
        notebookName += ".snb";

    // ---- 读 document.json，找出外部 PDF，并准备改写后的 manifest ----
    QJsonObject patchedManifest;
    bool hasPatchedManifest = false;
    QList<QString> pdfPaths;
    QMap<QString, QString> pdfZipName;   // 绝对路径 -> zip 里的 embedded/文件名

    const QString manifestAbs = bundleDir.filePath(QStringLiteral("document.json"));
    QFile manifestFile(manifestAbs);
    if (manifestFile.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
        manifestFile.close();

        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            QJsonObject root = doc.object();
            pdfPaths = collectExternalPdfs(snbDir, root);

            // 分配 zip 内的 PDF 文件名，同名加序号避免冲突
            for (const QString& pdf : pdfPaths) {
                QString base = QFileInfo(pdf).fileName();
                QString candidate = base;
                int n = 1;
                while (pdfZipName.values().contains(candidate)) {
                    const int dot = base.lastIndexOf('.');
                    const QString stem = dot > 0 ? base.left(dot) : base;
                    const QString ext  = dot > 0 ? base.mid(dot) : QString();
                    candidate = QStringLiteral("%1_%2%3").arg(stem).arg(n++).arg(ext);
                }
                pdfZipName.insert(pdf, candidate);
            }

            if (!pdfPaths.isEmpty()) {
                // legacy 镜像指向主 PDF，和上游导出格式一致
                const QString& primary = pdfPaths.first();
                root[QStringLiteral("pdf_relative_path")] =
                    QStringLiteral("../embedded/") + pdfZipName.value(primary);
                root[QStringLiteral("pdf_path")] =
                    QStringLiteral("../embedded/") + pdfZipName.value(primary);

                // 多 PDF 源：逐个改写
                if (root.value(QStringLiteral("pdf_sources")).isArray()) {
                    QJsonArray sources = root.value(QStringLiteral("pdf_sources")).toArray();
                    for (int i = 0; i < sources.size(); ++i) {
                        if (!sources.at(i).isObject())
                            continue;
                        QJsonObject src = sources.at(i).toObject();
                        rewritePdfPath(src, QStringLiteral("path"), snbDir, pdfZipName);
                        rewritePdfPath(src, QStringLiteral("relative_path"), snbDir, pdfZipName);
                        sources[i] = src;
                    }
                    root[QStringLiteral("pdf_sources")] = sources;
                }

                patchedManifest = root;
                hasPatchedManifest = true;
                qInfo() << "[SyncMod] will embed" << pdfPaths.size() << "PDF(s)";
            }
        }
    }

    // ---- 内存 ZIP ----
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
        if (errorMessage) *errorMessage = QStringLiteral("无法初始化 ZIP writer");
        return out;
    }

    bool addFailed = false;
    QDirIterator it(snbDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = bundleDir.relativeFilePath(it.filePath());

        QByteArray data;
        if (hasPatchedManifest && rel == QStringLiteral("document.json")) {
            data = QJsonDocument(patchedManifest).toJson(QJsonDocument::Indented);
        } else {
            QFile file(it.filePath());
            if (!file.open(QIODevice::ReadOnly)) {
                addFailed = true;
                break;
            }
            data = file.readAll();
            file.close();
        }

        const QString zipEntry = notebookName + "/" + rel;
        const QByteArray entryUtf8 = zipEntry.toUtf8();
        if (!mz_zip_writer_add_mem(&zip, entryUtf8.constData(),
                                   data.constData(), data.size(),
                                   MZ_DEFAULT_COMPRESSION)) {
            addFailed = true;
            break;
        }
    }

    // ---- 追加外部 PDF（已压缩，不再二次压缩，和上游一致）----
    if (!addFailed) {
        for (const QString& pdf : pdfPaths) {
            QFile file(pdf);
            if (!file.open(QIODevice::ReadOnly)) {
                qWarning() << "[SyncMod] failed to read PDF:" << pdf;
                continue;
            }
            const QByteArray data = file.readAll();
            file.close();

            const QString zipEntry = QStringLiteral("embedded/") + pdfZipName.value(pdf);
            const QByteArray entryUtf8 = zipEntry.toUtf8();
            if (!mz_zip_writer_add_mem(&zip, entryUtf8.constData(),
                                       data.constData(), data.size(),
                                       MZ_NO_COMPRESSION)) {
                qWarning() << "[SyncMod] failed to add PDF to zip:" << pdf;
            } else {
                qInfo() << "[SyncMod] embedded PDF:" << zipEntry
                        << "(" << data.size() << "bytes)";
            }
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

    QString safeName = remoteName;
    if (!safeName.endsWith(".snbx", Qt::CaseInsensitive))
        safeName += ".snbx";
    safeName = QFileInfo(safeName).fileName();   // 防路径穿越

    const QString path = QDir::temp().filePath("speedynote-sync-" + safeName);
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
