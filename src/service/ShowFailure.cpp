// SPDX-License-Identifier: MIT
// @author zhouwr
#include "ShowFailure.h"

namespace dsh::service {

void classifyShowFailure(const QString& stderrText, const QString& unitName,
                         RejectionReason& reason, QString& detail) {
    const QString err = stderrText.trimmed();
    const bool notFound = err.contains(QLatin1String("could not be found"))
        || err.contains(QLatin1String("No such file or directory"));
    const bool busDown = err.contains(QLatin1String("bus"), Qt::CaseInsensitive)
        || err.contains(QLatin1String("connect"), Qt::CaseInsensitive);
    if (notFound) {
        reason = RejectionReason::UnitNotFound;
        detail = unitName + QStringLiteral(" 在该范围未发现");
    } else if (busDown) {
        reason = RejectionReason::BusUnavailable;
        detail = err.isEmpty() ? QStringLiteral("systemd 总线不可用") : err;
    } else {
        reason = RejectionReason::ShowFailed;
        detail = err.isEmpty() ? QStringLiteral("systemctl show 失败") : err;
    }
}

}  // namespace dsh::service
