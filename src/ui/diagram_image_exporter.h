#pragma once

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QUrl>

class QQuickWindow;

namespace yauml {

class DiagramCanvas;
class ProjectController;

// Renders a complete diagram through the production Qt Quick canvas and writes
// the resulting raster image. The temporary render surface keeps export state
// out of the interactive canvas and provides the same geometry, text, clipping,
// filtering, styling, and connector rendering as the editor.
class DiagramImageExporter : public QObject {
  Q_OBJECT
  Q_PROPERTY(ProjectController *project READ project WRITE setProject NOTIFY
                 projectChanged)
  Q_PROPERTY(QString diagramId READ diagramId WRITE setDiagramId NOTIFY
                 diagramIdChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
  explicit DiagramImageExporter(QObject *parent = nullptr);
  ~DiagramImageExporter() override;

  ProjectController *project() const;
  void setProject(ProjectController *project);
  QString diagramId() const;
  void setDiagramId(const QString &diagramId);
  bool busy() const;

  Q_INVOKABLE void exportPng(const QUrl &fileUrl, qreal requestedScale = 1.0);

signals:
  void projectChanged();
  void diagramIdChanged();
  void busyChanged();
  void exportSucceeded(const QUrl &fileUrl, const QSize &pixelSize);
  void exportFailed(const QString &message);

private:
  void capture(quint64 serial);
  void fail(const QString &message, quint64 serial);
  void resetSurface();
  void setBusy(bool busy);

  ProjectController *m_project = nullptr;
  QString m_diagramId;
  bool m_busy = false;
  QPointer<QQuickWindow> m_window;
  QPointer<DiagramCanvas> m_canvas;
  QString m_outputPath;
  QSize m_targetPixelSize;
  int m_renderedFrames = 0;
  bool m_captureScheduled = false;
  quint64 m_exportSerial = 0;
};

} // namespace yauml
