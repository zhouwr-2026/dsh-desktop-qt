// SPDX-License-Identifier: MIT
// @author zhouwr

#include "ServiceDiscovery.h"

#include "ShowFailure.h"
#include "SystemctlShowParser.h"
#include "../util/RunSyncProcess.h"

#include <QStandardPaths>

namespace dsh::service {

namespace {
constexpr int kSystemctlShowTimeoutMs = 3000;

/// 对单个域执行一次只读 ``systemctl show``。
///
/// \p ok       出参：调用是否成功。
/// \p reason   出参：失败时的拒绝原因（成功时置 None）。
/// \p detail   出参：人类可读的诊断信息。
/// 返回 stdout 文本；失败时返回空串。
QString runSystemctlShow(ServiceScope scope, const QString& unitName,
                         bool& ok, RejectionReason& reason, QString& detail) {
    const QString exe = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (exe.isEmpty()) {
        ok = false;
        reason = RejectionReason::SystemctlMissing;
        detail = QStringLiteral("找不到 systemctl 可执行文件");
        return {};
    }

    QStringList args;
    if (scope == ServiceScope::User) args << QStringLiteral("--user");
    args << QStringLiteral("--no-pager") << QStringLiteral("show") << unitName;

    // 统一通过 runSyncProcess：超时自动 kill，避免旧版"waitForStarted +
    // waitForFinished + kill + waitForFinished"样板。
    const auto probe = dsh::util::runSyncProcess(
        exe, args, kSystemctlShowTimeoutMs, /*killGraceMs=*/500);
    if (!probe.startedOk) {
        ok = false;
        reason = RejectionReason::ProcessError;
        detail = QStringLiteral("无法启动 systemctl 进程");
        return {};
    }
    if (!probe.finishedOk) {
        ok = false;
        reason = RejectionReason::Timeout;
        detail = QStringLiteral("systemctl show 超时");
        return {};
    }

    const QString out = QString::fromLocal8Bit(probe.stdoutBytes);
    if (probe.crashed || probe.exitCode != 0) {
        ok = false;
        const QString err = QString::fromLocal8Bit(probe.stderrBytes).trimmed();
        classifyShowFailure(err, unitName, reason, detail);
        return {};
    }
    ok = true;
    reason = RejectionReason::None;
    detail.clear();
    return out;
}

}  // namespace

DiscoveredService validateCandidate(const ServiceInfo& info) {
    DiscoveredService candidate;
    candidate.info = info;
    if (info.loadState != QLatin1String("loaded")) {
        candidate.valid = false;
        candidate.rejection = RejectionReason::LoadStateNotLoaded;
        candidate.rejectionDetail =
            QStringLiteral("LoadState=%1（应为 loaded）").arg(info.loadState);
        return candidate;
    }
    if (!info.invokesOfficialDshWeb) {
        candidate.valid = false;
        candidate.rejection = RejectionReason::ForeignExecStart;
        candidate.rejectionDetail =
            QStringLiteral("ExecStart 不调用官方 dsh web");
        return candidate;
    }
    candidate.valid = true;
    candidate.rejection = RejectionReason::None;
    candidate.rejectionDetail.clear();
    return candidate;
}

int selectCandidateIndex(const QVector<DiscoveredService>& candidates) {
    int systemIndex = -1;
    int userIndex = -1;
    for (int i = 0; i < candidates.size(); ++i) {
        if (!candidates.at(i).valid) continue;
        if (candidates.at(i).info.scope == ServiceScope::User) {
            userIndex = i;
        } else {
            systemIndex = i;
        }
    }
    // 文档化选择偏好：两者都有效时优先用户级；否则选唯一的有效候选；
    // 均无效时不选中（-1）。
    if (userIndex >= 0) return userIndex;
    if (systemIndex >= 0) return systemIndex;
    return -1;
}

const DiscoveredService* DiscoveryResult::selected() const {
    if (selectedIndex >= 0 && selectedIndex < candidates.size()) {
        return &candidates.at(selectedIndex);
    }
    return nullptr;
}

DetectedService DiscoveryResult::detected() const {
    DetectedService result;
    const DiscoveredService* sel = selected();
    if (!sel || !sel->valid) return result;
    result.valid = true;
    result.unitName = sel->info.unitName;
    result.scope = sel->info.scope;
    result.host = sel->info.host;
    result.port = sel->info.port;
    return result;
}

DiscoveryResult discoverDshWebService(const QString& unitName) {
    DiscoveryResult result;
    result.systemctlAvailable =
        !QStandardPaths::findExecutable(QStringLiteral("systemctl")).isEmpty();

    const QVector<ServiceScope> scopes = {ServiceScope::System, ServiceScope::User};
    for (const ServiceScope scope : scopes) {
        bool ok = false;
        RejectionReason reason = RejectionReason::None;
        QString detail;
        const QString show = runSystemctlShow(scope, unitName, ok, reason, detail);

        DiscoveredService candidate;
        candidate.info.unitName = unitName;
        candidate.info.scope = scope;
        if (!ok) {
            candidate.valid = false;
            candidate.rejection = reason;
            candidate.rejectionDetail = detail;
            result.candidates.append(candidate);
            continue;
        }
        candidate = validateCandidate(parseSystemctlShow(show, unitName, scope));
        result.candidates.append(candidate);
    }

    result.selectedIndex = selectCandidateIndex(result.candidates);
    return result;
}

}  // namespace dsh::service
