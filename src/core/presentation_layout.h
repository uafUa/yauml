#pragma once

#include "core/project_data.h"

#include <QRectF>
#include <QSizeF>

namespace yauml::presentation_layout {

// These measurements describe persisted scene geometry, not device pixels.
// Keep the renderer and all geometry-producing commands on this shared set so
// content fitting remains stable as nodes move between creation paths.
inline constexpr qreal kNodeHeaderHeight = 30.0;
inline constexpr qreal kNodeLineHeight = 21.0;
inline constexpr qreal kNodeTextPadding = 8.0;
inline constexpr qreal kNodeBottomPadding = 6.0;
inline constexpr qreal kMinimumNodeWidth = 120.0;
inline constexpr qreal kMinimumNodeHeight = 60.0;
inline constexpr qreal kFixedNodeWidth = 220.0;
inline constexpr qreal kFixedNodeHeight = 120.0;

inline constexpr qreal kContainerHeaderHeight = 32.0;
inline constexpr qreal kContainerHorizontalPadding = 24.0;
inline constexpr qreal kContainerTopPadding = 52.0;
inline constexpr qreal kContainerBottomPadding = 24.0;
inline constexpr qreal kMinimumContainerWidth = 240.0;
inline constexpr qreal kMinimumContainerHeight = 100.0;

QSizeF nodeContentSize(const ModelElement &element);
QSizeF nodeContentSize(const ProjectData &project, const ModelElement &element);
QSizeF nodeContentSize(const ProjectData &project, const ModelElement &element,
                       bool showAttributes, bool showOperations,
                       OperationSignatureMode operationSignatureMode =
                           OperationSignatureMode::Full);
// Measures a node using the exact title rendered in its current diagram
// context. This prevents a namespace frame's hidden prefix from inflating a
// fitted presentation.
QSizeF nodeContentSizeForDisplayName(
    const ProjectData &project, const ModelElement &element,
    const QString &displayName, bool showAttributes, bool showOperations,
    OperationSignatureMode operationSignatureMode =
        OperationSignatureMode::Full);
QSizeF nodePlacementSize(const ModelElement &element,
                         const QString &sizingMode);
QSizeF nodePlacementSize(const ProjectData &project,
                         const ModelElement &element,
                         const QString &sizingMode);

// Returns the semantic qualified name while tolerating both storage forms
// used by projects: imported elements already carry qualified names, whereas
// user-created elements can carry a short name plus an explicit packageId.
QString fullyQualifiedElementName(const ProjectData &project,
                                  const ModelElement &element);

// A type shown inside a package frame does not need to repeat that frame's
// namespace prefix. Diagram root and unrelated package frames use the full
// semantic name so presentation-only movement never obscures its real owner.
QString elementDisplayNameInPackage(const ProjectData &project,
                                    const ModelElement &element,
                                    const QString &packageElementId);

// Finds the closest package frame containing a presentation, including
// package frames reached through intervening folder/container frames.
QString containingPackageElementId(const Diagram &diagram,
                                   const QString &presentationId);

QString containerDisplayName(const ProjectData &project,
                             const ContainerPresentation &container);

// Includes the text padding needed by the package-tab/container-header
// renderer. Package titles use their semantic fully qualified name.
qreal containerTitleWidth(const ProjectData &project,
                          const ContainerPresentation &container);

QRectF containerContentGeometry(const ProjectData &project,
                                const Diagram &diagram,
                                const ContainerPresentation &container);

} // namespace yauml::presentation_layout
