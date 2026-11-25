#pragma once

// ============================================================================
// NotebookPackage - .snb 文件夹 <-> .snbx 字节流 转换
// ============================================================================
// 同步专用：直接用 miniz 把磁盘上的 .snb 文件夹打包成 .snbx 字节数组，
// 不加载 Document 到内存（性能/内存优先，符合旧设备目标）。
// 解包复用上游 NotebookImporter 的逻辑约定（由宿主完成导入）。
// ============================================================================

#include <QByteArray>
#include <QString>
#include <QList>
#include <QDateTime>

namespace SyncMod {

class NotebookPackage {
public:
    struct Info {
        QString name;          // 笔记本显示名（文件夹名去掉 .snb）
        QString bundlePath;    // 本地 .snb 文件夹绝对路径
        QDateTime modified;    // 最后修改时间
    };

    // 扫描目录下所有 .snb 文件夹
    static QList<Info> scanNotebooks(const QString& rootDir);

    // 把一个 .snb 文件夹打包成 .snbx 字节流（内存Zip）
    // snbDir: 以 .snb 结尾的文件夹路径
    static QByteArray packBundle(const QString& snbDir, bool* ok = nullptr,
                                 QString* errorMessage = nullptr);

    // 把 .snbx 字节流解到临时文件，返回文件路径（调用方负责删除）
    // 导入由宿主调 NotebookImporter::importPackage 完成
    static QString unpackToTempFile(const QByteArray& snbxData,
                                    const QString& remoteName,
                                    bool* ok = nullptr,
                                    QString* errorMessage = nullptr);
};

} // namespace SyncMod
