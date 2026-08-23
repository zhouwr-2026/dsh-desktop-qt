// SPDX-License-Identifier: MIT
// @author zhouwr
#include "UpdatePlan.h"

#include <algorithm>

namespace dsh::updater {

namespace {

const QString kBackendSource = QStringLiteral("npm 注册表（@deepseek-ai/dsh）");
const QString kDesktopSource = QStringLiteral("Gitee 发布（dsh-desktop-qt）");

}  // namespace

ComponentUpdate backendComponent(const Status& backend) {
    ComponentUpdate out;
    out.component = UpdateComponent::Backend;
    out.current = backend.current;
    out.target = backend.latest;
    out.source = kBackendSource;

    if (backend.updateAvailable) {
        out.state = ComponentState::Available;
        return out;
    }

    const bool currentEmpty = backend.current.isEmpty();
    const bool targetEmpty = backend.latest.isEmpty();

    if (currentEmpty && targetEmpty) {
        out.state = ComponentState::Invalid;  // 无任何可用信息（离线且未知）
        return out;
    }
    if (!currentEmpty && targetEmpty) {
        out.state = ComponentState::Invalid;  // 已知当前版本但拿不到目标（离线）
        return out;
    }
    if (currentEmpty && !targetEmpty) {
        out.state = ComponentState::Unavailable;  // 后端未安装，无更新对象
        return out;
    }

    // current 与 latest 均非空：若任一非法 SemVer，则结果不可用。
    if (!isValidSemVer(backend.current) || !isValidSemVer(backend.latest)) {
        out.state = ComponentState::Invalid;
        return out;
    }

    // 两者均是合法 SemVer 且 updateAvailable == false：latest <= current，已最新。
    out.state = ComponentState::Current;
    return out;
}

ComponentUpdate desktopComponent(const DesktopVersionResult& desktop) {
    ComponentUpdate out;
    out.component = UpdateComponent::Desktop;
    out.source = kDesktopSource;

    switch (desktop.status) {
        case VersionCheckStatus::Ok:
            out.target = desktop.release.tagName;
            // Result 不携带桌面本地版本，只有 updateAvailable 这一权威标志：
            // 有更新 -> Available，否则视为已最新。
            out.state = desktop.updateAvailable ? ComponentState::Available
                                                : ComponentState::Current;
            break;
        case VersionCheckStatus::NoRelease:
            out.state = ComponentState::Unavailable;  // 发布源无可用版本
            break;
        case VersionCheckStatus::Offline:
        case VersionCheckStatus::InvalidResponse:
            out.state = ComponentState::Invalid;  // 响应不可用
            break;
    }
    return out;
}

UpdatePlan::UpdatePlan(QVector<ComponentUpdate> components)
    : components_(std::move(components)) {}

UpdatePlan UpdatePlan::combine(const Status& backend, const DesktopVersionResult& desktop) {
    QVector<ComponentUpdate> components;
    // 后端优先：无论输入顺序如何，Backend 总是先于 Desktop 存放。
    components.append(backendComponent(backend));
    components.append(desktopComponent(desktop));
    return UpdatePlan(std::move(components));
}

const ComponentUpdate* UpdatePlan::component(UpdateComponent c) const {
    for (const ComponentUpdate& update : components_) {
        if (update.component == c) return &update;
    }
    return nullptr;
}

bool UpdatePlan::trayActionVisible() const {
    return std::any_of(components_.cbegin(), components_.cend(),
                       [](const ComponentUpdate& u) {
                           return u.state == ComponentState::Available;
                       });
}

QVector<UpdateComponent> UpdatePlan::defaultSelected() const {
    // 组件已按后端优先存放，直接依序收集 Available 组件即可保证后端优先。
    QVector<UpdateComponent> selected;
    for (const ComponentUpdate& update : components_) {
        if (update.state == ComponentState::Available) {
            selected.append(update.component);
        }
    }
    return selected;
}

QVector<UpdateComponent> UpdatePlan::orderedAvailable() const {
    return defaultSelected();
}

}  // namespace dsh::updater
