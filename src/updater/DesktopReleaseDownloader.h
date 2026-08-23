// SPDX-License-Identifier: MIT
// @author zhouwr
//
// DSH Desktop —— Gitee 发布附件下载器。
//
// 与只读版本检查器（``DesktopVersionChecker``）不同，本类负责把用户在发布
// 中选中（或由 ``selectBestAsset`` 推荐）的一个 ``DesktopReleaseAsset``
// 异步下载到本机受控缓存，并计算 SHA-256。
//
// 边界原则：
//   * **只下载，不替换、不启动**。本类把文件落到缓存目录并给出校验结果，
//     实际安装/替换交给 ``DesktopUpdateHelper::atomicReplace`` 等独立逻辑；
//   * 下载遵循 Qt Network 异步模型，通过 ``stageChanged`` / ``progressChanged`` /
//     ``finished`` 三个信号向调用方上报状态，绝不阻塞调用线程；
//   * 仅接受 **HTTPS + Gitee 域名** 的附件 URL，其余一律拒绝；
//   * 附件文件名经过净化，杜绝路径穿越（见 ``sanitizeFileName``）；
//   * 写入通过 ``QSaveFile`` 原子落盘；**成功后才**对缓存文件计算 SHA-256，
//     以校验写盘结果与网络字节一致。
//
// 纯函数（资产选择 / URL 校验 / 文件名净化 / 平台判定）均不触发网络，
// 不读写文件系统，可被单元测试稳定覆盖；异步网络路径在测试中刻意不触发。

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVector>

#include "DesktopVersionChecker.h"

QT_BEGIN_NAMESPACE
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;
QT_END_NAMESPACE

namespace dsh::updater {

/// 一次下载的强类型结果。
struct DesktopDownloadResult {
    QString sourceUrl;   ///< 下载源 URL（原始 ``asset.url``；空表示从未发起）
    QString cachedPath;  ///< 成功落盘后的文件绝对路径；失败时为空
    qint64 size{0};      ///< 成功时实际写入的字节数
    QString sha256;      ///< 成功时的小写十六进制摘要（64 字符）；失败时为空
    QString error;       ///< 空表示成功，否则为简要诊断
    bool ok() const { return error.isEmpty() && !cachedPath.isEmpty(); }
};

/// 异步 Gitee 发布附件下载器。
class DesktopReleaseDownloader : public QObject {
    Q_OBJECT
public:
    /// @param cachePath 缓存根目录。留空（默认）时使用
    ///        ``QStandardPaths::CacheLocation``。
    explicit DesktopReleaseDownloader(QObject* parent = nullptr);
    explicit DesktopReleaseDownloader(const QString& cachePath, QObject* parent = nullptr);
    ~DesktopReleaseDownloader() override;

    /// 设置缓存根目录（覆盖默认的 CacheLocation）。用于测试时显式指定临时目录。
    void setCachePath(const QString& path) { cachePath_ = path; }
    QString cachePath() const { return cachePath_; }

    /// 默认缓存根目录：``QStandardPaths::CacheLocation``（Linux 下形如
    /// ``~/.cache/<应用>``），可能为空字符串表示当前环境无缓存目录。
    static QString defaultCachePath();

    // ------------------------------------------------------------------
    // 纯函数 —— 无网络、无文件系统，可单元测试
    // ------------------------------------------------------------------

    /// 判定 URL 是否允许作为下载源：必须是 HTTPS，且主机为 gitee.com 或其子域。
    /// \return 允许返回 ``true``；否则返回 ``false`` 并（可选）写入 ``reason``。
    static bool isAllowedUrl(const QUrl& url, QString* reason = nullptr);
    static bool isAllowedUrl(const QString& urlString, QString* reason = nullptr);

    /// 净化附件文件名，返回安全的单层文件名。
    /// 拒绝：空名、NUL 字节、任何路径分隔符（``/`` 或 ``\``）、``.`` / ``..``、
    /// C++ ``QDir::cleanPath`` 会改写（即不是纯净文件名）的名字、首尾空白。
    /// \return 净化后的文件名；不可用时返回空串并（可选）写入 ``error``。
    static QString sanitizeFileName(const QString& assetName, QString* error = nullptr);

    /// 当前主机操作系统名（``linux`` / ``macos`` / ``windows`` 等粗粒度值）。
    static QString hostOsName();

    /// 当前主机 CPU 架构（``QSysInfo::currentCpuArchitecture``，小写）。
    static QString hostArchitecture();

    /// 给单个资产打分，用于在同发布的多附件里做平台化选择。
    /// \return 分数越高越优先；返回 ``std::numeric_limits<int>::lowest()``
    ///         表示该资产不可选（辅助/校验文件、URL 非法等）。
    static int assetScore(const DesktopReleaseAsset& asset,
                          const QString& osName = hostOsName(),
                          const QString& architecture = hostArchitecture());

    /// 从一批附件中选出最匹配当前平台的一个。
    /// 纯函数：不触发网络、不读写文件系统。同分取第一个（稳定）。
    /// \return 指向 ``assets`` 内最佳资产的指针；没有合适资产时返回 ``nullptr``。
    ///         指针生命周期跟随 ``assets``，调用方负责保证其存续。
    static const DesktopReleaseAsset* selectBestAsset(
        const QVector<DesktopReleaseAsset>& assets,
        const QString& osName = hostOsName(),
        const QString& architecture = hostArchitecture());

    // ------------------------------------------------------------------
    // 异步下载
    // ------------------------------------------------------------------

    /// 开始下载指定资产。
    ///
    /// 同步校验（URL / 文件名净化 / 缓存目录）通过后返回 ``true`` 并转入异步；
    /// 之后必然以一次 ``finished`` 收尾（成功 ``result.ok()`` 或失败携带
    /// ``result.error``）。同步校验失败时返回 ``false`` 且**不**发射任何信号，
    /// 拒绝原因可通过 ``lastError()`` 读取。
    bool start(const DesktopReleaseAsset& asset);

    /// 是否有正在进行的下载。
    bool isActive() const { return reply_ != nullptr; }

    /// 取消进行中的下载；由于 ``reply->abort()``，会以 ``finished``（带错误）收尾。
    void cancel();

    /// 最近一次同步失败的原因（上次 ``start`` 返回 ``false`` 时的诊断）。
    QString lastError() const { return lastError_; }

signals:
    /// 阶段变化：``downloading`` / ``finalizing``。
    void stageChanged(const QString& stage);

    /// 下载进度：已接收字节数与总字节数（未知时为 -1）。
    void progressChanged(qint64 bytesReceived, qint64 bytesTotal);

    /// 下载收尾（成功或失败各一次），携带强类型结果。
    void finished(const dsh::updater::DesktopDownloadResult& result);

private slots:
    void onReadyRead();
    void onProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onFinished();

private:
    /// 生效的缓存根（显式指定优先，否则默认 CacheLocation）。
    QString effectiveCacheRoot() const;

    /// 把已净化的单层文件名安全地拼到缓存根下，结果必须是缓存根内的绝对路径。
    bool safeCachePath(const QString& fileName, QString* outPath, QString* error) const;

    /// 终止并清理进行中的下载资源（网络回复与 QSaveFile）。
    void cleanup();

    QString cachePath_;                ///< 显式缓存根；空表示用默认
    QNetworkAccessManager* nam_;       ///< 本类私有网络管理器（父对象为 this）
    QNetworkReply* reply_ = nullptr;   ///< 当前网络回复
    QSaveFile* saveFile_ = nullptr;    ///< 当前写入目标（原子落盘）
    QString lastError_;                ///< 最近一次同步拒绝原因
    QString sourceUrl_;                ///< 当前下载源 URL
    QString targetPath_;               ///< 当前目标绝对路径
    qint64 bytesReceived_ = 0;         ///< 已写入文件的实际字节数
    qint64 bytesTotal_ = -1;           ///< 内容总长；-1 表示未知
};

}  // namespace dsh::updater
