#include "SyncManager.h"

#include "backends/WebDavBackend.h"
#include "backends/OneDriveBackend.h"

#include <QDir>
#include <QFileInfo>

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

    if (m_pendingEntries.isEmpty()) {
        finishSync(true, QStringLiteral("远端为空，无需同步"));
        return;
    }

    // 逐个拉取远端笔记本（简化版：先全部拉取，宿主负责冲突判断）
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

    // 通知宿主应用远端快照（宿主实现导入逻辑）；若宿主未连接信号则忽略
    emit applyRemoteSnapshot(entry.path, data);

    ++m_pendingIndex;
    if (m_pendingIndex < m_pendingEntries.size()) {
        m_backend->download(m_pendingEntries.at(m_pendingIndex));
    } else {
        finishSync(true, QStringLiteral("同步完成，共 %1 个笔记本").arg(m_pendingEntries.size()));
    }
}

void SyncManager::onUploaded(const QString& path, const QString& etag,
                             bool ok, const QString& message)
{
    Q_UNUSED(path)
    Q_UNUSED(etag)
    if (!ok)
        finishSync(false, message);
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
