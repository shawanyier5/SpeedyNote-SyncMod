#include "SyncConfig.h"

#include <QDir>
#include <QStandardPaths>

namespace SyncMod {

SyncConfig::SyncConfig(QObject* parent)
    : QObject(parent)
    , m_settings("SpeedyNote", "SyncMod")
{
}

void SyncConfig::setPlatform(Platform p)
{
    if (m_platform != p) {
        m_platform = p;
        emit changed();
    }
}

void SyncConfig::setIntervalMinutes(int minutes)
{
    // 0 = 手动同步；1..1440 = 每 N 分钟
    int clamped = qBound(0, minutes, 1440);
    if (m_intervalMinutes != clamped) {
        m_intervalMinutes = clamped;
        emit changed();
    }
}

void SyncConfig::setLocalNotebookDir(const QString& dir)
{
    if (m_localNotebookDir != dir) {
        m_localNotebookDir = dir;
        emit changed();
    }
}

void SyncConfig::setRemotePrefix(const QString& prefix)
{
    if (m_remotePrefix != prefix) {
        m_remotePrefix = prefix;
        emit changed();
    }
}

void SyncConfig::setSyncOnStartup(bool enabled)
{
    if (m_syncOnStartup != enabled) {
        m_syncOnStartup = enabled;
        emit changed();
    }
}

void SyncConfig::setAutoResolveConflict(bool enabled)
{
    if (m_autoResolveConflict != enabled) {
        m_autoResolveConflict = enabled;
        emit changed();
    }
}

void SyncConfig::setWebdavUrl(const QString& url)
{
    if (m_webdavUrl != url) {
        m_webdavUrl = url;
        emit changed();
    }
}

void SyncConfig::setWebdavUsername(const QString& user)
{
    if (m_webdavUsername != user) {
        m_webdavUsername = user;
        emit changed();
    }
}

void SyncConfig::setWebdavPassword(const QString& pass)
{
    if (m_webdavPassword != pass) {
        m_webdavPassword = pass;
        emit changed();
    }
}

void SyncConfig::setOnedriveLocalPath(const QString& path)
{
    if (m_onedriveLocalPath != path) {
        m_onedriveLocalPath = path;
        emit changed();
    }
}

// QSettings 的 key 不能带 '/'，远端名理论上只是文件名，这里再做一次清洗兜底
static QString etagKey(const QString& remoteName)
{
    QString key = remoteName;
    key.replace('/', '_');
    key.replace('\\', '_');
    return key;
}

QString SyncConfig::etagFor(const QString& remoteName) const
{
    if (remoteName.isEmpty())
        return QString();
    m_settings.beginGroup("etags");
    const QString value = m_settings.value(etagKey(remoteName), QString()).toString();
    m_settings.endGroup();
    return value;
}

void SyncConfig::setEtag(const QString& remoteName, const QString& etag)
{
    if (remoteName.isEmpty())
        return;
    m_settings.beginGroup("etags");
    if (etag.isEmpty())
        m_settings.remove(etagKey(remoteName));
    else
        m_settings.setValue(etagKey(remoteName), etag);
    m_settings.endGroup();
    m_settings.sync();   // 立即落盘：崩溃后 ETag 丢失会导致重复拉同一本书
    emit changed();
}

void SyncConfig::removeEtag(const QString& remoteName)
{
    setEtag(remoteName, QString());
}

void SyncConfig::load()
{
    m_platform = static_cast<Platform>(m_settings.value("platform", 0).toInt());
    m_intervalMinutes = m_settings.value("intervalMinutes", 15).toInt();
    m_localNotebookDir = m_settings.value("localNotebookDir", QString()).toString();
    m_remotePrefix = m_settings.value("remotePrefix", "/SpeedyNote").toString();
    m_syncOnStartup = m_settings.value("syncOnStartup", true).toBool();
    m_autoResolveConflict = m_settings.value("autoResolveConflict", false).toBool();

    m_webdavUrl = m_settings.value("webdav/url", QString()).toString();
    m_webdavUsername = m_settings.value("webdav/username", QString()).toString();
    m_webdavPassword = m_settings.value("webdav/password", QString()).toString();
    m_onedriveLocalPath = m_settings.value("onedrive/localPath", QString()).toString();
}

void SyncConfig::save()
{
    m_settings.setValue("platform", static_cast<int>(m_platform));
    m_settings.setValue("intervalMinutes", m_intervalMinutes);
    m_settings.setValue("localNotebookDir", m_localNotebookDir);
    m_settings.setValue("remotePrefix", m_remotePrefix);
    m_settings.setValue("syncOnStartup", m_syncOnStartup);
    m_settings.setValue("autoResolveConflict", m_autoResolveConflict);

    m_settings.setValue("webdav/url", m_webdavUrl);
    m_settings.setValue("webdav/username", m_webdavUsername);
    m_settings.setValue("webdav/password", m_webdavPassword);
    m_settings.setValue("onedrive/localPath", m_onedriveLocalPath);

    m_settings.sync();
}

} // namespace SyncMod
