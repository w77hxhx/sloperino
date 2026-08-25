// SPDX-License-Identifier: MIT
// The Limerino feature catalog: the single source of truth for the settings
// page wiki AND the anti-drift tests (tests/src/LimerinoFeatures.cpp).
// Every entry must correspond to code that exists - add features in the same
// commit as the code, with their non-command access paths.

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace chatterino::limerino {

enum class FeatureStatus {
    Implemented,
    Partial,
    Planned,
    Unavailable,
};

enum class AccessKind {
    Command,
    ContextMenu,
    Dialog,
    ToolbarButton,
    SettingsToggle,
    Automatic,
};

struct FeatureAccess {
    AccessKind kind;
    QString detail;  // user-readable path, e.g. "split 3-dot menu > Filter events..."
};

struct LimerinoFeature {
    QString id;
    QString name;
    QString category;
    QString summary;
    QString description;
    FeatureStatus status = FeatureStatus::Implemented;
    QList<FeatureAccess> access;
    QStringList commands;
    QStringList requirements;
    QString settingsAnchor;
};

// The complete catalog, in display order. `Ctrl` accessors.
const QList<LimerinoFeature> &limerinoFeatures();
const LimerinoFeature *findLimerinoFeature(const QString &id);

}  // namespace chatterino::limerino
