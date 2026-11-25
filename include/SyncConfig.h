#pragma once

// ============================================================================
// SyncConfig - 同步配置 (库 B: SpeedyNote-SyncMod)
// ============================================================================
// 用户可在设置面板配置：
// - 同步平台（WebDAV / OneDrive）
// - 同步间隔（每 N 分钟，0 = 手动）
// - 本地笔记本根目录
// - 远端路径前缀
// 配置存 QSettings，与 SpeedyNote 主配置分离，前缀 SyncMod/
// ============================================================================

#include <QObject>
#include <QString>
#include <QSettings>

namespace SyncMod {

class SyncConfig : public QObject {
    Q_OBJECT

public:
    enum class Platform {
        Disabled = 0,
        WebDAV   = 1,
        OneDrive = 2
    };
    Q_ENUM(Platform)

    explicit SyncConfig(QObject* parent = nullptr);

    // ---- 通用配置 ----
    Platform platform() const { return m_platform; }
    void setPlatform(Platform p);

    int intervalMinutes() const { return m_intervalMinutes; }
    void setIntervalMinutes(int minutes);   // 0=手动, 否则 1..1440

    QString localNotebookDir() const { return m_localNotebookDir; }
    void setLocalNotebookDir(const QString& dir);

    QString remotePrefix() const { return m_remotePrefix; }
    void setRemotePrefix(const QString& prefix);

    bool syncOnStartup() const { return m_syncOnStartup; }
    void setSyncOnStartup(bool enabled);

    bool autoResolveConflict() const { return m_autoResolveConflict; }
    void setAutoResolveConflict(bool enabled);

    // ---- WebDAV 配置 ----
    QString webdavUrl() const { return m_webdavUrl; }
    void setWebdavUrl(const QString& url);

    QString webdavUsername() const { return m_webdavUsername; }
    void setWebdavUsername(const QString& user);

    QString webdavPassword() const { return m_webdavPassword; }
    void setWebdavPassword(const QString& pass);

    // ---- OneDrive 配置 ----
    // 桌面端：指向 OneDrive 本地同步文件夹
    // 安卓端：指向用户通过系统文件选择器授权的 OneDrive 目录
    QString onedriveLocalPath() const { return m_onedriveLocalPath; }
    void setOnedriveLocalPath(const QString& path);

    // ---- 上次同步的远端版本标识 ----
    // 远端没变就跳过拉取，避免每轮定时同步都重复导入/备份同一本书
    QString etagFor(const QString& remoteName) const;
    void setEtag(const QString& remoteName, const QString& etag);
    void removeEtag(const QString& remoteName);

    // ---- 持久化 ----
    void load();
    void save();

signals:
    void changed();

private:
    mutable QSettings m_settings;

    Platform m_platform = Platform::Disabled;
    int m_intervalMinutes = 15;
    QString m_localNotebookDir;
    QString m_remotePrefix = "/SpeedyNote";
    bool m_syncOnStartup = true;
    bool m_autoResolveConflict = false;

    QString m_webdavUrl;
    QString m_webdavUsername;
    QString m_webdavPassword;
    QString m_onedriveLocalPath;
};

} // namespace SyncMod
