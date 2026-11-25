#pragma once

// ============================================================================
// WebDavBackend - WebDAV 同步后端
// ============================================================================
// 兼容坚果云 / Nextcloud / 群晖 NAS 等标准 WebDAV 服务。
// 用 QNetworkAccessManager 发 PROPFIND / GET / PUT / DELETE。
// 认证：Basic（坚果云用应用密码，不是账号密码）。
// ============================================================================

#include "SyncBackend.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>

namespace SyncMod {

class SyncConfig;

class WebDavBackend : public SyncBackend {
    Q_OBJECT

public:
    explicit WebDavBackend(SyncConfig* config, QObject* parent = nullptr);
    ~WebDavBackend() override;

    QString id() const override { return QStringLiteral("webdav"); }
    QString displayName() const override { return QStringLiteral("WebDAV（坚果云 / Nextcloud / NAS）"); }

    void testConnection() override;
    void listRemote() override;
    void download(const RemoteEntry& entry) override;
    void upload(const QString& remotePath, const QByteArray& snbxData,
                const QString& etagToReplace = QString()) override;
    void remove(const QString& remotePath) override;

private:
    QNetworkRequest makeRequest(const QUrl& url) const;
    QUrl fullUrl(const QString& remotePath) const;
    void parsePropfind(const QByteArray& xml, QList<RemoteEntry>& out) const;

    SyncConfig* m_config = nullptr;
    QNetworkAccessManager* m_net = nullptr;
    RemoteEntry m_currentDownload;
};

} // namespace SyncMod
