#include "OneDriveBackend.h"
#include "SyncConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

namespace SyncMod {

OneDriveBackend::OneDriveBackend(SyncConfig* config, QObject* parent)
    : SyncBackend(parent)
    , m_config(config)
{
}

OneDriveBackend::~OneDriveBackend() = default;

QString OneDriveBackend::rootPath() const
{
    QString base = m_config->onedriveLocalPath();
    QString prefix = m_config->remotePrefix();
    if (base.isEmpty())
        return QString();

    QDir dir(base);
    while (!prefix.isEmpty() && (prefix.startsWith('/') || prefix.startsWith('\\')))
        prefix.remove(0, 1);
    if (!prefix.isEmpty())
        dir.cd(prefix);
    return dir.absolutePath();
}

void OneDriveBackend::testConnection()
{
    QString root = rootPath();
    QDir dir(root);
    bool ok = !root.isEmpty() && dir.exists();
    QString msg = ok
        ? QStringLiteral("连接成功：%1").arg(root)
        : QStringLiteral("目录不存在或不可访问：%1").arg(root);
    emit connectionTested(ok, msg);
}

void OneDriveBackend::listRemote()
{
    QString root = rootPath();
    QDir dir(root);
    if (root.isEmpty() || !dir.exists()) {
        emit remoteListed({}, false, QStringLiteral("OneDrive 目录未配置或不可访问"));
        return;
    }

    QList<RemoteEntry> entries;
    QStringList filters;
    filters << "*.snbx";
    for (const QFileInfo& fi : dir.entryInfoList(filters, QDir::Files, QDir::Time)) {
        RemoteEntry e;
        e.name = fi.fileName();
        e.path = e.name;
        e.size = fi.size();
        e.modified = fi.lastModified();
        e.etag = QString::number(fi.lastModified().toMSecsSinceEpoch());
        entries.append(e);
    }
    emit remoteListed(entries, true, QString());
}

void OneDriveBackend::download(const RemoteEntry& entry)
{
    QString root = rootPath();
    QString filePath = QDir(root).filePath(entry.name);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit downloaded(entry, QByteArray(), false,
                        QStringLiteral("读取失败：%1").arg(file.errorString()));
        return;
    }
    QByteArray data = file.readAll();
    file.close();
    emit progress(data.size(), data.size());
    emit downloaded(entry, data, true, QString());
}

void OneDriveBackend::upload(const QString& remotePath, const QByteArray& snbxData,
                             const QString& etagToReplace)
{
    Q_UNUSED(etagToReplace)  // 本地文件夹模式用文件时间戳判断，不用 etag 头

    QString root = rootPath();
    QDir().mkpath(root);
    QString filePath = QDir(root).filePath(remotePath);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit uploaded(remotePath, QString(), false,
                      QStringLiteral("写入失败：%1").arg(file.errorString()));
        return;
    }
    file.write(snbxData);
    file.close();
    QString newEtag = QString::number(
        QFileInfo(filePath).lastModified().toMSecsSinceEpoch());
    emit progress(snbxData.size(), snbxData.size());
    emit uploaded(remotePath, newEtag, true, QString());
}

void OneDriveBackend::remove(const QString& remotePath)
{
    QString filePath = QDir(rootPath()).filePath(remotePath);
    bool ok = QFile::remove(filePath);
    emit removed(remotePath, ok,
                 ok ? QString() : QStringLiteral("删除失败：%1").arg(filePath));
}

} // namespace SyncMod
