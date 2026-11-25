#pragma once

// ============================================================================
// OneDriveBackend - OneDrive 同步后端（本地文件夹模式）
// ============================================================================
// 设计取舍：
// 不走 Microsoft Graph API（需要 OAuth、服务端、token 刷新，违反"轻量"原则），
// 而是同步 OneDrive 客户端的本地文件夹：
// - Win11：OneDrive 客户端已把文件夹同步到本地，直接读写
// - 安卓：用户通过系统文件选择器授权 OneDrive 目录（SAF），App 直接读写
// 两端逻辑完全一致，都是普通文件 IO，无网络代码，内存占用极低。
// ============================================================================

#include "SyncBackend.h"

namespace SyncMod {

class SyncConfig;

class OneDriveBackend : public SyncBackend {
    Q_OBJECT

public:
    explicit OneDriveBackend(SyncConfig* config, QObject* parent = nullptr);
    ~OneDriveBackend() override;

    QString id() const override { return QStringLiteral("onedrive"); }
    QString displayName() const override { return QStringLiteral("OneDrive（本地文件夹）"); }

    void testConnection() override;
    void listRemote() override;
    void download(const RemoteEntry& entry) override;
    void upload(const QString& remotePath, const QByteArray& snbxData,
                const QString& etagToReplace = QString()) override;
    void remove(const QString& remotePath) override;

private:
    QString rootPath() const;   // OneDrive 本地根目录 + remotePrefix

    SyncConfig* m_config = nullptr;
};

} // namespace SyncMod
