# SpeedyNote-SyncMod（库 B：同步模块）

> **出处声明**：本模块由 **AI（WorkBuddy）辅助重构**编写，服务于原项目 [alpha-liu-01/SpeedyNote](https://github.com/alpha-liu-01/SpeedyNote)（GPL-3.0，原作者 alpha-liu-01）的派生版本。非原作者作品，问题请勿打扰原作者。配套本体见 `SpeedyNote-PluginHost` 仓库。

给 SpeedyNote 提供多端同步能力的独立模块。**编译期静态链接**到库 A（插件版软件本体），无动态库加载开销、无运行时脚本引擎，符合"旧设备也能跑"的原则。

## 设计原则

1. **零侵入**：只依赖 Qt6 Core/Network，**不引用任何 SpeedyNote 内部头文件**。上游 SpeedyNote 内部怎么重构都不影响本库。
2. **可配置间隔**：每 N 分钟自动同步一次（1–1440 分钟），0 = 纯手动。用 `Qt::VeryCoarseTimer` 省电。
3. **平台可切换**：WebDAV / OneDrive 两套后端，配置面板一键切换。
4. **通信靠信号**：需要宿主保存/导入笔记本时发信号（`requestLocalSnapshot` / `applyRemoteSnapshot`），宿主决定怎么实现。

## 目录结构

```
include/
  SyncBackend.h    同步后端纯虚接口（list/download/upload/remove）
  SyncConfig.h     配置：平台、间隔、凭据、本地目录
  SyncManager.h    调度器：定时器 + 后端选择 + 同步流程
backends/
  WebDavBackend.h/.cpp     WebDAV 实现（PROPFIND/GET/PUT/DELETE，Basic 认证）
  OneDriveBackend.h/.cpp   OneDrive 本地文件夹实现（纯文件 IO，无网络）
src/
  SyncConfig.cpp / SyncManager.cpp
```

## 两种同步方案

### 方案一：WebDAV（推荐跨平台）
- 兼容坚果云、Nextcloud、群晖 NAS 等标准 WebDAV
- 坚果云要用**应用密码**，不是账号密码
- Win11 和华为 MatePad 用同一套逻辑，行为一致

### 方案二：OneDrive（本地文件夹模式）
- **不走 Microsoft Graph API**（避免 OAuth、token 刷新、后台服务的开销）
- Win11：OneDrive 客户端已同步到本地，直接读写那个文件夹
- 安卓：用户通过系统文件选择器授权 OneDrive 目录，App 直接读写
- 两端逻辑完全一致，纯文件 IO，内存占用极低

## 配置存储

QSettings，组织名 `SpeedyNote`、应用名 `SyncMod`，与主人配置分离：

| 键 | 说明 | 默认 |
|---|---|---|
| `platform` | 0=关闭 1=WebDAV 2=OneDrive | 0 |
| `intervalMinutes` | 同步间隔（分钟），0=手动 | 15 |
| `syncOnStartup` | 启动时同步一次 | true |
| `autoResolveConflict` | 自动解决冲突（关=保留两份） | false |
| `webdav/url` | WebDAV 地址 | 空 |
| `webdav/username` / `webdav/password` | 凭据（密码建议用应用密码） | 空 |
| `onedrive/localPath` | OneDrive 本地文件夹路径 | 空 |
| `remotePrefix` | 远端路径前缀 | `/SpeedyNote` |

## 冲突处理规则

`.snb` 是文件夹结构（含图块和 `document.json`），同步时打包成 `.snbx`。两端同时改同一笔记本时：
- 比较 `etag`（WebDAV）或文件时间戳（OneDrive）
- 默认**保留两份**（不覆盖任何一边），文件名加后缀区分
- 开启 `autoResolveConflict` 后按最新时间覆盖（有丢笔画风险，慎用）

## 与库 A 的关系

库 A 在 CMake 里 `add_subdirectory(../SpeedyNote-SyncMod ...)` 引入本库，静态链接。库 A 只需：
1. 创建 `SyncConfig` 和 `SyncManager`
2. 连接 `applyRemoteSnapshot` 信号到自己的导入逻辑
3. 在 `requestLocalSnapshot` 信号里把自己的笔记本打成 `.snbx`
4. 提供一个设置面板让用户填配置（平台/间隔/凭据）
