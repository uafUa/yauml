#pragma once

#include <QVector>

namespace uuml::ui {

struct TriangleBatch {
  qsizetype firstVertex = 0;
  qsizetype vertexCount = 0;
};

// Splits a triangle-list vertex stream into backend-safe draw ranges. Keeping
// every range triangle-aligned preserves primitive ordering across QSG nodes.
QVector<TriangleBatch>
triangleBatches(qsizetype vertexCount,
                qsizetype maximumVerticesPerBatch = 60'000);

} // namespace uuml::ui
