#include "ui/triangle_batch.h"

#include <algorithm>

namespace uuml::ui {

QVector<TriangleBatch> triangleBatches(qsizetype vertexCount,
                                       qsizetype maximumVerticesPerBatch) {
  Q_ASSERT(vertexCount >= 0);
  Q_ASSERT(vertexCount % 3 == 0);
  Q_ASSERT(maximumVerticesPerBatch >= 3);
  if (vertexCount <= 0 || maximumVerticesPerBatch < 3)
    return {};

  maximumVerticesPerBatch -= maximumVerticesPerBatch % 3;
  QVector<TriangleBatch> batches;
  batches.reserve(static_cast<qsizetype>(
      (vertexCount + maximumVerticesPerBatch - 1) / maximumVerticesPerBatch));
  for (qsizetype first = 0; first < vertexCount;) {
    const qsizetype count =
        std::min(maximumVerticesPerBatch, vertexCount - first);
    batches.append({first, count});
    first += count;
  }
  return batches;
}

} // namespace uuml::ui
