#pragma once

// ============================================================================
// SyncManager - 同步调度器 (库 B: SpeedyNote-SyncMod)
// ============================================================================
// 职责：
// - 按 SyncConfig 的间隔定时触发同步（QTimer，每 N 分钟）
// - 选择当前平台对应的后端
// - 执行双向同步、检测冲突
// - 不碰 SpeedyNote 内部对象，只通过 signal 通知宿主保存/刷新
// ============================================================================

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QList>
#include <QString>

#include "SyncBackend.h"
#include "SyncConfig.h"

namespace SyncMod {

class SyncManager : public QObject {
    Q_OBJECT

public:
    explicit SyncManager(SyncConfig* config, QObject* parent = nullptr);
    ~SyncManager() override;

    // 由宿主在启动时调用，按配置启动定时器
    void start();

    // 停止定时器
    void stop();

    // 立即执行一次同步（手动触发）
    void syncNow();

    // 当前后端（供设置面板做连接测试）
    SyncBackend* currentBackend() const { return m_backend; }

    // 上次同步状态
    QDateTime lastSyncTime() const { return m_lastSyncTime; }
    bool lastSyncOk() const { return m_lastSyncOk; }
    QString lastSyncMessage() const { return m_lastSyncMessage; }

signals:
    // 同步生命周期
    void syncStarted();
    void syncFinished(bool ok, const QString& message);
    void conflictsDetected(const QList<Conflict>& conflicts);

    // 需要宿主配合：把某个笔记本落盘为 .snbx（宿主实现）
    // relativePath 用于匹配，宿主回填 bytes
    void requestLocalSnapshot(const QString& relativePath, QByteArray* outData);
    // 需要宿主配合：用 .snbx 数据覆盖/导入本地笔记本
    void applyRemoteSnapshot(const QString& relativePath, const QByteArray& data);

private slots:
    void onTimerFire();
    void onRemoteListed(const QList<RemoteEntry>& entries, bool ok, const QString& message);
    void onDownloaded(const RemoteEntry& entry, const QByteArray& data,
                      bool ok, const QString& message);
    void onUploaded(const QString& path, const QString& etag,
                    bool ok, const QString& message);

private:
    void rebuildBackend();
    void doSync();
    void finishSync(bool ok, const QString& message);

    SyncConfig* m_config = nullptr;
    SyncBackend* m_backend = nullptr;
    QTimer m_timer;

    QList<RemoteEntry> m_pendingEntries;
    int m_pendingIndex = 0;

    QDateTime m_lastSyncTime;
    bool m_lastSyncOk = false;
    QString m_lastSyncMessage;

    bool m_syncing = false;
};

} // namespace SyncMod
