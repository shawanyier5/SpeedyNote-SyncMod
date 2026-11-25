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
#include "NotebookPackage.h"

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
    // 需要宿主配合：把远端拉下来的 .snbx 导入本地
    // remoteName : 远端文件名，如 "MyNote.snbx"
    // tmpSnbxPath: 已落盘的临时 .snbx 路径，宿主 importPackage 后负责删除
    // etag       : 远端版本标识，导入成功后由宿主回填 SyncConfig
    void applyRemoteSnapshot(const QString& remoteName,
                             const QString& tmpSnbxPath,
                             const QString& etag);

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

    // 阶段 2：扫描本地 .snb 并逐个打包上传
    void startUploadPhase();
    void uploadNextLocal();

    SyncConfig* m_config = nullptr;
    SyncBackend* m_backend = nullptr;
    QTimer m_timer;

    QList<RemoteEntry> m_pendingEntries;
    int m_pendingIndex = 0;

    // 本轮从远端拉下来的文件名，上传阶段跳过，避免原样推回
    QStringList m_downloadedThisRound;

    // 同步阶段：0=列目录 1=拉远端 2=传本地
    int m_phase = 0;
    int m_uploadIndex = 0;
    QList<NotebookPackage::Info> m_localNotebooks;

    QDateTime m_lastSyncTime;
    bool m_lastSyncOk = false;
    QString m_lastSyncMessage;

    bool m_syncing = false;
};

} // namespace SyncMod
