#include "WebDavBackend.h"
#include "SyncConfig.h"

#include <QXmlStreamReader>
#include <QUrlQuery>

namespace SyncMod {

WebDavBackend::WebDavBackend(SyncConfig* config, QObject* parent)
    : SyncBackend(parent)
    , m_config(config)
    , m_net(new QNetworkAccessManager(this))
{
}

WebDavBackend::~WebDavBackend() = default;

QNetworkRequest WebDavBackend::makeRequest(const QUrl& url) const
{
    QNetworkRequest req(url);
    // Basic 认证：坚果云等 WebDAV 服务用应用密码
    QString credentials = m_config->webdavUsername() + ":" + m_config->webdavPassword();
    QByteArray basic = "Basic " + credentials.toUtf8().toBase64();
    req.setRawHeader("Authorization", basic);
    req.setHeader(QNetworkRequest::UserAgentHeader, "SpeedyNote-SyncMod/1.0");
    return req;
}

QUrl WebDavBackend::fullUrl(const QString& remotePath) const
{
    QUrl base(m_config->webdavUrl());
    QString prefix = m_config->remotePrefix();
    QString path = base.path();
    if (!prefix.isEmpty()) {
        if (!path.endsWith('/'))
            path += '/';
        QString cleanPrefix = prefix;
        while (cleanPrefix.startsWith('/'))
            cleanPrefix.remove(0, 1);
        path += cleanPrefix;
    }
    if (!remotePath.isEmpty()) {
        if (!path.endsWith('/'))
            path += '/';
        QString cleanPath = remotePath;
        while (cleanPath.startsWith('/'))
            cleanPath.remove(0, 1);
        path += cleanPath;
    }
    base.setPath(path);
    return base;
}

void WebDavBackend::testConnection()
{
    QNetworkRequest req = makeRequest(fullUrl(QString()));
    req.setRawHeader("Depth", "0");
    QNetworkReply* reply = m_net->sendCustomRequest(req, "PROPFIND");

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = (status >= 200 && status < 300);
        QString msg = ok
            ? QStringLiteral("连接成功")
            : QStringLiteral("连接失败：HTTP %1 %2")
                  .arg(status).arg(reply->errorString());
        emit connectionTested(ok, msg);
    });
}

void WebDavBackend::listRemote()
{
    QNetworkRequest req = makeRequest(fullUrl(QString()));
    req.setRawHeader("Depth", "1");
    QNetworkReply* reply = m_net->sendCustomRequest(req, "PROPFIND");

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status < 200 || status >= 300) {
            emit remoteListed({}, false,
                QStringLiteral("列目录失败：HTTP %1").arg(status));
            return;
        }

        QList<RemoteEntry> entries;
        parsePropfind(reply->readAll(), entries);
        // 只保留 .snbx 笔记本包
        QList<RemoteEntry> filtered;
        for (const RemoteEntry& e : entries) {
            if (e.name.endsWith(".snbx", Qt::CaseInsensitive))
                filtered.append(e);
        }
        emit remoteListed(filtered, true, QString());
    });
}

void WebDavBackend::parsePropfind(const QByteArray& xml, QList<RemoteEntry>& out) const
{
    QXmlStreamReader reader(xml);
    RemoteEntry current;
    bool inResponse = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            QString name = reader.name().toString();
            if (name == "response") {
                inResponse = true;
                current = RemoteEntry();
            } else if (inResponse && name == "href") {
                QString href = reader.readElementText();
                // URL 解码 + 取最后一段作为名字
                QUrl hrefUrl(QUrl::fromPercentEncoding(href.toUtf8()));
                QString path = hrefUrl.path();
                current.path = path;
                int slash = path.lastIndexOf('/');
                current.name = (slash >= 0 && slash < path.length() - 1)
                    ? path.mid(slash + 1) : path;
            } else if (inResponse && name == "getcontentlength") {
                current.size = reader.readElementText().toLongLong();
            } else if (inResponse && name == "getlastmodified") {
                current.modified = QDateTime::fromString(
                    reader.readElementText(), Qt::RFC2822Date);
                current.modified.setTimeSpec(Qt::UTC);
            } else if (inResponse && (name == "etag" || name == "getetag")) {
                current.etag = reader.readElementText();
            }
        } else if (reader.isEndElement() && reader.name() == "response") {
            if (current.isValid())
                out.append(current);
            inResponse = false;
        }
    }
}

void WebDavBackend::download(const RemoteEntry& entry)
{
    m_currentDownload = entry;
    QNetworkRequest req = makeRequest(fullUrl(entry.name));
    QNetworkReply* reply = m_net->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 done, qint64 total) { emit progress(done, total); });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = (status >= 200 && status < 300);
        emit downloaded(m_currentDownload, ok ? reply->readAll() : QByteArray(),
                        ok, ok ? QString() : QStringLiteral("下载失败：HTTP %1").arg(status));
    });
}

void WebDavBackend::upload(const QString& remotePath, const QByteArray& snbxData,
                           const QString& etagToReplace)
{
    QNetworkRequest req = makeRequest(fullUrl(remotePath));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/zip");
    if (!etagToReplace.isEmpty())
        req.setRawHeader("If-Match", etagToReplace.toUtf8());

    QNetworkReply* reply = m_net->put(req, snbxData);

    connect(reply, &QNetworkReply::uploadProgress, this,
            [this](qint64 done, qint64 total) { emit progress(done, total); });

    connect(reply, &QNetworkReply::finished, this, [this, reply, remotePath]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = (status >= 200 && status < 300);
        QString etag = QString::fromUtf8(reply->rawHeader("ETag"));
        emit uploaded(remotePath, etag, ok,
                      ok ? QString() : QStringLiteral("上传失败：HTTP %1").arg(status));
    });
}

void WebDavBackend::remove(const QString& remotePath)
{
    QNetworkRequest req = makeRequest(fullUrl(remotePath));
    QNetworkReply* reply = m_net->sendCustomRequest(req, "DELETE");

    connect(reply, &QNetworkReply::finished, this, [this, reply, remotePath]() {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = (status >= 200 && status < 300);
        emit removed(remotePath, ok,
                     ok ? QString() : QStringLiteral("删除失败：HTTP %1").arg(status));
    });
}

} // namespace SyncMod
