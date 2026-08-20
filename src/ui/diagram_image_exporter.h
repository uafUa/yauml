#pragma once

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QUrl>
#include <QVariantMap>

class QQuickWindow;

namespace yauml {

class DiagramCanvas;
class ProjectController;

// Exports a complete diagram. PNG/PDF use the production Qt Quick canvas;
// SVG/Draw.io use a shared vector-document scene so both illustration formats
// retain the same persisted layout without rasterizing text or geometry.
class DiagramImageExporter : public QObject {
  Q_OBJECT
  Q_PROPERTY(ProjectController *project READ project WRITE setProject NOTIFY
                 projectChanged)
  Q_PROPERTY(QString diagramId READ diagramId WRITE setDiagramId NOTIFY
                 diagramIdChanged)
  Q_PROPERTY(bool typeIconsVisible READ typeIconsVisible WRITE
                 setTypeIconsVisible NOTIFY typeIconsVisibleChanged)
  Q_PROPERTY(QVariantMap typeIconSources READ typeIconSources WRITE
                 setTypeIconSources NOTIFY typeIconSourcesChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
  explicit DiagramImageExporter(QObject *parent = nullptr);
  ~DiagramImageExporter() override;

  ProjectController *project() const;
  void setProject(ProjectController *project);
  QString diagramId() const;
  void setDiagramId(const QString &diagramId);
  bool typeIconsVisible() const;
  void setTypeIconsVisible(bool visible);
  QVariantMap typeIconSources() const;
  void setTypeIconSources(const QVariantMap &sources);
  bool busy() const;

  Q_INVOKABLE void exportPng(const QUrl &fileUrl, qreal requestedScale = 1.0);
  Q_INVOKABLE void exportPdf(const QUrl &fileUrl, qreal requestedScale = 1.0);
  Q_INVOKABLE void exportSvg(const QUrl &fileUrl);
  Q_INVOKABLE void exportDrawio(const QUrl &fileUrl);

signals:
  void projectChanged();
  void diagramIdChanged();
  void typeIconsVisibleChanged();
  void typeIconSourcesChanged();
  void busyChanged();
  void exportSucceeded(const QUrl &fileUrl, const QSize &pixelSize);
  void exportFailed(const QString &message);

private:
  enum class OutputFormat { Png, Pdf };

  void exportDiagram(const QUrl &fileUrl, qreal requestedScale,
                     OutputFormat format);
  void exportDocument(const QUrl &fileUrl, const QString &suffix,
                      const QString &formatName, bool drawio);
  void capture(quint64 serial);
  void fail(const QString &message, quint64 serial);
  void resetSurface();
  void setBusy(bool busy);

  ProjectController *m_project = nullptr;
  QString m_diagramId;
  bool m_typeIconsVisible = false;
  QVariantMap m_typeIconSources;
  bool m_busy = false;
  QPointer<QQuickWindow> m_window;
  QPointer<DiagramCanvas> m_canvas;
  QString m_outputPath;
  OutputFormat m_outputFormat = OutputFormat::Png;
  QSize m_targetPixelSize;
  int m_renderedFrames = 0;
  bool m_captureScheduled = false;
  quint64 m_exportSerial = 0;
};

} // namespace yauml
