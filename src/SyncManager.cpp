#include "SyncManager.h"

#include "WebDavBackend.h"
#include "OneDriveBackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

namespace SyncMod {

SyncManager::SyncManager(SyncConfig* config, QObject* parent)
    : QObject(parent)
    , m_config(config)
{
    m_timer.setSingleShot(false);
    m_timer.setTimerType(Qt::VeryCoarseTimer);  // 省电：分钟级同步用粗粒度定时器

    connect(&m_timer, &QTimer::timeout, this, &SyncManager::onTimerFire);
}

SyncManager::~SyncManager()
{
    stop();
}

void SyncManager::start()
{
    rebuildBackend();
    if (!m_backend)
        return;

    if (m_config->intervalMinutes() > 0) {
        m_timer.setInterval(m_config->intervalMinutes() * 60 * 1000);
        m_timer.start();
    } else {
        m_timer.stop();
    }

    if (m_config->syncOnStartup())
        QTimer::singleShot(2000, this, &SyncManager::syncNow);
}

void SyncManager::stop()
{
    m_timer.stop();
}

void SyncManager::syncNow()
{
    if (m_syncing || !m_backend || m_config->platform() == SyncConfig::Platform::Disabled)
        return;
    doSync();
}

void SyncManager::rebuildBackend()
{
    if (m_backend) {
        m_backend->deleteLater();
        m_backend = nullptr;
    }

    switch (m_config->platform()) {
    case SyncConfig::Platform::WebDAV: {
        auto* b = new WebDavBackend(m_config, this);
        connect(b, &SyncBackend::remoteListed, this, &SyncManager::onRemoteListed);
        connect(b, &SyncBackend::downloaded, this, &SyncManager::onDownloaded);
        connect(b, &SyncBackend::uploaded, this, &SyncManager::onUploaded);
        m_backend = b;
        break;
    }
    case SyncConfig::Platform::OneDrive: {
        auto* b = new OneDriveBackend(m_config, this);
        connect(b, &SyncBackend::remoteListed, this, &SyncManager::onRemoteListed);
        connect(b, &SyncBackend::downloaded, this, &SyncManager::onDownloaded);
        connect(b, &SyncBackend::uploaded, this, &SyncManager::onUploaded);
        m_backend = b;
        break;
    }
    default:
        break;
    }
}

void SyncManager::onTimerFire()
{
    // 配置可能在运行中改了间隔，重建定时器
    if (m_config->intervalMinutes() > 0) {
        int expected = m_config->intervalMinutes() * 60 * 1000;
        if (m_timer.interval() != expected)
            m_timer.setInterval(expected);
    }
    syncNow();
}

void SyncManager::doSync()
{
    m_syncing = true;
    m_phase = 0;
    m_downloadedThisRound.clear();
    emit syncStarted();
    m_backend->listRemote();
}

void SyncManager::onRemoteListed(const QList<RemoteEntry>& entries, bool ok, const QString& message)
{
    if (!ok) {
        finishSync(false, message);
        return;
    }

    m_pendingEntries = entries;
    m_pendingIndex = 0;

    // 阶段 1：拉取远端不存在的笔记本（简化策略：全部拉取，宿主端做冲突判断）
    if (m_pendingEntries.isEmpty()) {
        m_phase = 2;
        startUploadPhase();
        return;
    }

    const RemoteEntry& entry = m_pendingEntries.at(m_pendingIndex);
    m_backend->download(entry);
}

void SyncManager::onDownloaded(const RemoteEntry& entry, const QByteArray& data,
                               bool ok, const QString& message)
{
    if (!ok) {
        finishSync(false, message);
        return;
    }

    // 远端版本没变就不重复导入，否则每轮定时同步都会多出一份备份。
    // 注意：这里不加入 m_downloadedThisRound，让本地若有改动仍有机会在阶段 2 上传。
    const QString knownEtag = m_config->etagFor(entry.name);
    if (!entry.etag.isEmpty() && !knownEtag.isEmpty() && entry.etag == knownEtag) {
        qInfo() << "[SyncMod] remote unchanged, skip:" << entry.name;
    } else {
        // 解包成临时 .snbx 文件，通知宿主导入（宿主接 NotebookImporter）
        bool unpackOk = false;
        const QString tmpPath =
            NotebookPackage::unpackToTempFile(data, entry.name, &unpackOk, nullptr);
        if (unpackOk) {
            m_downloadedThisRound.append(entry.name);
            // 信号里带的是临时文件路径，宿主负责 importPackage 后清理
            emit applyRemoteSnapshot(entry.name, tmpPath, entry.etag);
        } else {
            qWarning() << "[SyncMod] unpack failed:" << entry.name;
            QFile::remove(tmpPath);
        }
    }

    ++m_pendingIndex;
    if (m_pendingIndex < m_pendingEntries.size()) {
        m_backend->download(m_pendingEntries.at(m_pendingIndex));
    } else {
        // 阶段 2：扫描本地并上传
        m_phase = 2;
        startUploadPhase();
    }
}

void SyncManager::startUploadPhase()
{
    QString root = m_config->localNotebookDir();
    m_localNotebooks = NotebookPackage::scanNotebooks(root);
    m_uploadIndex = 0;

    if (m_localNotebooks.isEmpty()) {
        finishSync(true, QStringLiteral("同步完成：拉取 %1 个，本地无新增")
                             .arg(m_downloadedThisRound.size()));
        return;
    }
    uploadNextLocal();
}

void SyncManager::uploadNextLocal()
{
    if (m_uploadIndex >= m_localNotebooks.size()) {
        finishSync(true, QStringLiteral("同步完成：拉取 %1 个，上传 %2 个")
                             .arg(m_downloadedThisRound.size())
                             .arg(m_localNotebooks.size()));
        return;
    }

    const NotebookPackage::Info& info = m_localNotebooks.at(m_uploadIndex);
    QString remoteName = info.name + ".snbx";

    // 跳过本轮刚从远端拉下来的，避免原样推回
    if (m_downloadedThisRound.contains(remoteName)) {
        ++m_uploadIndex;
        uploadNextLocal();
        return;
    }

    bool packOk = false;
    QString err;
    QByteArray snbx = NotebookPackage::packBundle(info.bundlePath, &packOk, &err);
    if (!packOk) {
        qWarning() << "[SyncMod] pack failed:" << info.name << err;
        ++m_uploadIndex;
        uploadNextLocal();
        return;
    }

    m_backend->upload(remoteName, snbx);
}

void SyncManager::onUploaded(const QString& path, const QString& etag,
                             bool ok, const QString& message)
{
    if (!ok) {
        qWarning() << "[SyncMod] upload failed:" << path << message;
    } else if (!etag.isEmpty()) {
        // 记住刚推上去的版本，下轮就不会把它当"远端更新"再拉回来
        m_config->setEtag(path, etag);
    }
    ++m_uploadIndex;
    uploadNextLocal();
}

void SyncManager::finishSync(bool ok, const QString& message)
{
    m_syncing = false;
    m_lastSyncTime = QDateTime::currentDateTime();
    m_lastSyncOk = ok;
    m_lastSyncMessage = message;
    emit syncFinished(ok, message);
}

} // namespace SyncMod
