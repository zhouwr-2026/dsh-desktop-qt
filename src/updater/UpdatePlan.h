// SPDX-License-Identifier: MIT
// @author zhouwr
//
// 统一的更新计划模型（纯函数，无 UI、无网络、无进程）。
//
// 桌面端存在两类可更新组件：
//   * 后端（Backend）  —— 系统里安装的 dsh CLI，来源为 npm 注册表，
//                         由 ``dsh::updater::Updater::check`` 产出 ``Status``。
//   * 桌面（Desktop）  —— DSH Desktop 应用本体，来源为公开 Gitee 仓库，
//                         由 ``DesktopVersionChecker::check`` 产出
//                         ``DesktopVersionResult``。
//
// 本模块把这两个独立来源的检查结果合并成一个 ``UpdatePlan``：给每个组件赋予
// 强类型状态（Current / Available / Unavailable / Invalid），统一回答上层 UI
// 关心的三个问题：
//   * 托盘的"更新到最新版"动作是否应可见（存在任一组件可更新）；
//   * 默认应选中哪些组件（即为"可更新"的那些，后端优先）；
//   * 当后端与桌面同时可更新时，后端必须排在前面（后端优先顺序）。
//
// 本文件不包含任何网络/进程/界面逻辑，全部为可单元测试的纯函数。

#pragma once

#include <QString>
#include <QVector>

#include "DesktopVersionChecker.h"
#include "Updater.h"

namespace dsh::updater {

/// 可更新的组件类别。
enum class UpdateComponent {
    Backend,  // dsh CLI（npm 注册表）
    Desktop,  // DSH Desktop 应用本体（Gitee 发布）
};

/// 单个组件在一次检查后的强类型状态。
enum class ComponentState {
    Current,      // 有已知版本且已是最新，无需更新
    Available,    // 检测到更新的目标版本，可更新
    Unavailable,  // 无法更新：后端未安装，或发布源没有可用版本
    Invalid,      // 检查结果不可用：离线、SemVer 非法、响应不完整
};

/// 单个组件的更新信息：包含当前/目标版本与来源。
struct ComponentUpdate {
    UpdateComponent component{UpdateComponent::Backend};
    ComponentState state{ComponentState::Invalid};
    QString current;            // 当前版本；桌面端为空串表示计划未知该版本
    QString target;             // 目标（最新）版本；桌面端为 release tag
    QString source;             // 版本来源（如 npm 注册表、Gitee 发布）
    DesktopReleaseInfo release; // 仅桌面组件：检查时解析出的发布信息（含附件），
                                // 供后续下载选中资产；后端组件恒为默认值。

    bool operator==(const ComponentUpdate& o) const {
        return component == o.component && state == o.state
            && current == o.current && target == o.target && source == o.source
            && release == o.release;
    }
    bool operator!=(const ComponentUpdate& o) const { return !(*this == o); }
};

/// 合并两个来源的检查结果得到的统一更新计划。
class UpdatePlan {
public:
    /// 组合后端（``dsh::updater::Status``）与桌面（``DesktopVersionResult``）
    /// 的检查结果。组件总是按"后端优先"顺序存放：先 Backend，后 Desktop。
    static UpdatePlan combine(const Status& backend, const DesktopVersionResult& desktop);

    /// 按存放顺序（后端优先）返回全部组件。
    const QVector<ComponentUpdate>& components() const { return components_; }

    /// 按类别查找组件；找不到返回 ``nullptr``。
    const ComponentUpdate* component(UpdateComponent c) const;

    /// 统一的"更新到最新版"托盘动作是否应可见：任一组件处于 Available。
    bool trayActionVisible() const;

    /// 是否存在任一可更新组件（等价于 ``trayActionVisible``）。
    bool hasAvailableUpdate() const { return trayActionVisible(); }

    /// 默认应选中的组件：所有处于 Available 的组件，按后端优先排序。
    /// 当后端与桌面同时可更新时，``Backend`` 排在 ``Desktop`` 之前。
    QVector<UpdateComponent> defaultSelected() const;

    /// 当前可更新的组件（Available），按后端优先排序。
    QVector<UpdateComponent> orderedAvailable() const;

private:
    explicit UpdatePlan(QVector<ComponentUpdate> components);

    QVector<ComponentUpdate> components_;
};

/// 从后端检查结果映射为单个组件更新信息（纯函数）。
/// \return 状态为 Current/Available/Unavailable/Invalid 之一。
ComponentUpdate backendComponent(const Status& backend);

/// 从桌面检查结果映射为单个组件更新信息（纯函数）。
/// \return 状态为 Ok 且有更新 -> Available；Ok 且无更新 -> Current；
///         NoRelease -> Unavailable；Offline/InvalidResponse -> Invalid。
ComponentUpdate desktopComponent(const DesktopVersionResult& desktop);

/// 组件的可读分类名（纯函数）：``后端`` / ``桌面``。
QString componentLabel(UpdateComponent component);

/// 单个组件的单行人类可读摘要（纯函数），供更新对话框展示。
/// 例如 ``当前 0.1.0 → 最新 0.1.1（npm 注册表（@deepseek-ai/dsh））``；
/// 状态为 Current / Unavailable / Invalid 时会附带相应描述。
QString componentDetail(const ComponentUpdate& component);

}  // namespace dsh::updater
