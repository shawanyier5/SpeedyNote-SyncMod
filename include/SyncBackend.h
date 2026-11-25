#pragma once

// ============================================================================
// SyncBackend - 同步后端抽象接口 (库 B: SpeedyNote-SyncMod)
// ============================================================================
// 设计目标：
// - 纯虚接口，WebDAV / OneDrive 各自实现
// - 只传简单 Qt 类型，不暴露 SpeedyNote 内部对象（保证上游重构不影响本库）
// - 全异步（信号槽），不阻塞 UI 线程
// - 编译期静态链接，无动态库加载开销
// ============================================================================

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QJsonObject>

namespace SyncMod {

// 远端文件条目
struct RemoteEntry {
    QString path;          // 远端相对路径，如 "Notebooks/物理.snbx"
    QString name;          // 显示名
    qint64 size = 0;       // 字节数
    QDateTime modified;    // 远端修改时间（UTC）
    QString etag;          // 版本标识（WebDAV 的 ETag / OneDrive 的 eTag）
    bool isValid() const { return !path.isEmpty(); }
};

// 同步动作结果
struct SyncResult {
    bool ok = false;
    QString message;       // 错误或提示信息
    QJsonObject details;   // 后端自定义细节（可选）
};

// 冲突信息：两端都改了同一个笔记本
struct Conflict {
    QString relativePath;
    QDateTime localModified;
    QDateTime remoteModified;
    QString localVersionId;
    QString remoteVersionId;
};

class SyncBackend : public QObject {
    Q_OBJECT

public:
    explicit SyncBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~SyncBackend() override = default;

    // 后端标识，用于配置和日志："webdav" / "onedrive"
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    // 连接测试（配置保存时调用）
    virtual void testConnection() = 0;

    // 列出远端笔记本（.snbx 包）
    virtual void listRemote() = 0;

    // 拉取指定远端条目，返回 .snbx 的字节数据
    virtual void download(const RemoteEntry& entry) = 0;

    // 推送本地 .snbx 字节数据到远端
    virtual void upload(const QString& remotePath,
                        const QByteArray& snbxData,
                        const QString& etagToReplace = QString()) = 0;

    // 删除远端条目
    virtual void remove(const QString& remotePath) = 0;

signals:
    void connectionTested(bool ok, const QString& message);
    void remoteListed(const QList<RemoteEntry>& entries, bool ok, const QString& message);
    void downloaded(const RemoteEntry& entry, const QByteArray& data,
                    bool ok, const QString& message);
    void uploaded(const QString& remotePath, const QString& newEtag,
                  bool ok, const QString& message);
    void removed(const QString& remotePath, bool ok, const QString& message);
    void progress(qint64 bytesDone, qint64 bytesTotal);
};

} // namespace SyncMod
