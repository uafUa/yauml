#pragma once

#include "core/project_data.h"

#include <QSize>
#include <QString>
#include <QVariantMap>

namespace yauml::ui {

enum class DiagramDocumentFormat { Svg, Drawio };

struct DiagramDocumentExportOptions {
  bool typeIconsVisible = false;
  QVariantMap typeIconSources;
  qreal margin = 32.0;
};

struct DiagramDocumentExportResult {
  bool success = false;
  QString error;
  // SVG and Draw.io are resolution independent. This size is the exported
  // document's scene-unit extent and is useful for diagnostics and UI status.
  QSize documentSize;
};

// Writes an illustration of the persisted diagram view. Both formats are
// generated from the same intermediate scene, so filtering, presentation
// geometry, connector routes, annotations, styles, notes, and sequence layout
// cannot drift between SVG and Draw.io implementations.
DiagramDocumentExportResult
exportDiagramDocument(const ProjectData &project, const Diagram &diagram,
                      const QString &path, DiagramDocumentFormat format,
                      const DiagramDocumentExportOptions &options = {});

} // namespace yauml::ui
