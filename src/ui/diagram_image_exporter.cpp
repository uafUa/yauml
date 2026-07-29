#include "ui/diagram_image_exporter.h"

#include "core/project_controller.h"
#include "core/project_data.h"
#include "ui/diagram_canvas.h"
#include "ui/ui_theme.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace yauml {
namespace {

constexpr qreal kExportMargin = 32.0;
constexpr int kMaximumImageDimension = 16384;
constexpr qint64 kMaximumImagePixels = 64LL * 1024LL * 1024LL;
// A window placed outside the virtual desktop may stop receiving continuous
// frame swaps after its first complete frame on Windows. All canvas geometry
// and text nodes are synchronized before that frame is swapped, so waiting for
// it is sufficient and avoids relying on animation-style rendering.
constexpr int kFramesBeforeCapture = 1;
constexpr int kExportTimeoutMilliseconds = 15000;

qreal boundedExportScale(const QSizeF &contentSize, qreal requestedScale) {
  if (contentSize.isEmpty() || requestedScale <= 0.0)
    return 0.0;

  qreal scale = requestedScale;
  scale = std::min(scale, static_cast<qreal>(kMaximumImageDimension) /
                              contentSize.width());
  scale = std::min(scale, static_cast<qreal>(kMaximumImageDimension) /
                              contentSize.height());
  const qreal area = contentSize.width() * contentSize.height();
  if (area > 0.0) {
    scale = std::min(scale,
                     std::sqrt(static_cast<qreal>(kMaximumImagePixels) / area));
  }
  return scale;
}

QString pngOutputPath(const QUrl &fileUrl) {
  if (!fileUrl.isLocalFile())
    return {};
  QString path = QDir::cleanPath(fileUrl.toLocalFile());
  if (QFileInfo(path).suffix().compare(QStringLiteral("png"),
                                       Qt::CaseInsensitive) != 0)
    path += QStringLiteral(".png");
  return path;
}

} // namespace

DiagramImageExporter::DiagramImageExporter(QObject *parent) : QObject(parent) {}

DiagramImageExporter::~DiagramImageExporter() {
  if (m_window)
    delete m_window;
}

ProjectController *DiagramImageExporter::project() const { return m_project; }

void DiagramImageExporter::setProject(ProjectController *project) {
  if (m_project == project)
    return;
  m_project = project;
  emit projectChanged();
}

QString DiagramImageExporter::diagramId() const { return m_diagramId; }

void DiagramImageExporter::setDiagramId(const QString &diagramId) {
  if (m_diagramId == diagramId)
    return;
  m_diagramId = diagramId;
  emit diagramIdChanged();
}

bool DiagramImageExporter::busy() const { return m_busy; }

void DiagramImageExporter::exportPng(const QUrl &fileUrl,
                                     qreal requestedScale) {
  if (m_busy)
    return;

  const quint64 serial = ++m_exportSerial;
  if (!m_project || !findDiagram(m_project->data(), m_diagramId)) {
    fail(QStringLiteral("Cannot export: the diagram is not available."),
         serial);
    return;
  }
  m_outputPath = pngOutputPath(fileUrl);
  if (m_outputPath.isEmpty()) {
    fail(QStringLiteral("PNG export requires a local file destination."),
         serial);
    return;
  }
  const QFileInfo outputInfo(m_outputPath);
  if (!outputInfo.absoluteDir().exists()) {
    fail(QStringLiteral("Cannot export PNG because the destination folder does "
                        "not exist: %1")
             .arg(outputInfo.absolutePath()),
         serial);
    return;
  }

  setBusy(true);
  m_window = new QQuickWindow;
  if (QScreen *screen = QGuiApplication::primaryScreen())
    m_window->setScreen(screen);
  m_window->setTitle(QStringLiteral("yauml diagram export"));
  m_window->setColor(ui::uiPalette().surface);
  m_window->setFlags(Qt::Tool | Qt::FramelessWindowHint |
                     Qt::WindowDoesNotAcceptFocus |
                     Qt::WindowTransparentForInput);

  m_canvas = new DiagramCanvas(m_window->contentItem());
  m_canvas->setParentItem(m_window->contentItem());
  m_canvas->setProject(m_project);
  m_canvas->setDiagramId(m_diagramId);
  m_canvas->setClip(true);

  const QRectF contentBounds = m_canvas->diagramContentBounds(kExportMargin);
  if (contentBounds.isEmpty()) {
    fail(QStringLiteral("Cannot export an empty diagram."), serial);
    return;
  }
  const qreal outputScale =
      boundedExportScale(contentBounds.size(), requestedScale);
  if (outputScale <= 0.0) {
    fail(QStringLiteral("Cannot determine a valid PNG export size."), serial);
    return;
  }

  m_targetPixelSize = {
      std::max(1, qCeil(contentBounds.width() * outputScale)),
      std::max(1, qCeil(contentBounds.height() * outputScale))};
  const qreal devicePixelRatio =
      std::max<qreal>(1.0, m_window->devicePixelRatio());
  const qreal logicalScale = outputScale / devicePixelRatio;
  const QSize logicalSize = {
      std::max(1, qCeil(contentBounds.width() * logicalScale)),
      std::max(1, qCeil(contentBounds.height() * logicalScale))};

  m_window->resize(logicalSize);
  m_canvas->setWidth(logicalSize.width());
  m_canvas->setHeight(logicalSize.height());
  m_canvas->configureExportViewport(contentBounds, logicalScale);

  // Qt Quick only exposes and renders native windows which intersect the
  // virtual desktop on Windows. Keep this non-activating tool surface on-screen
  // while making it effectively transparent; grabWindow() still captures the
  // underlying scene content rather than the composed desktop opacity.
  if (QScreen *screen = m_window->screen()) {
    const QRect screenGeometry = screen->geometry();
    m_window->setPosition(screenGeometry.topLeft());
    m_window->setOpacity(0.01);
  } else {
    m_window->setPosition(0, 0);
  }

  m_renderedFrames = 0;
  m_captureScheduled = false;
  connect(
      m_window, &QQuickWindow::frameSwapped, this,
      [this, serial] {
        if (serial != m_exportSerial || !m_window || m_captureScheduled)
          return;
        ++m_renderedFrames;
        if (m_renderedFrames < kFramesBeforeCapture) {
          m_window->requestUpdate();
          return;
        }
        m_captureScheduled = true;
        QTimer::singleShot(0, this, [this, serial] { capture(serial); });
      },
      Qt::QueuedConnection);

  QTimer::singleShot(kExportTimeoutMilliseconds, this, [this, serial] {
    if (m_busy && serial == m_exportSerial)
      fail(QStringLiteral("PNG export timed out while rendering the diagram."),
           serial);
  });
  m_window->show();
  m_window->requestUpdate();
}

void DiagramImageExporter::capture(quint64 serial) {
  if (serial != m_exportSerial || !m_window)
    return;

  QImage image = m_window->grabWindow();
  if (image.isNull()) {
    fail(QStringLiteral("Qt Quick could not capture the exported diagram."),
         serial);
    return;
  }
  if (image.width() < m_targetPixelSize.width() ||
      image.height() < m_targetPixelSize.height()) {
    fail(QStringLiteral("Qt Quick returned an incomplete export image."),
         serial);
    return;
  }
  if (image.size() != m_targetPixelSize)
    image = image.copy(QRect(QPoint(), m_targetPixelSize));
  image.setDevicePixelRatio(1.0);
  if (!image.save(m_outputPath, "PNG")) {
    fail(QStringLiteral("Could not write PNG export: %1").arg(m_outputPath),
         serial);
    return;
  }

  const QUrl outputUrl = QUrl::fromLocalFile(m_outputPath);
  const QSize pixelSize = image.size();
  if (m_project) {
    const auto *diagram = findDiagram(m_project->data(), m_diagramId);
    m_project->diagnostics()->addInfo(
        QStringLiteral("export"),
        QStringLiteral("Exported diagram \"%1\" to %2 (%3 × %4 pixels).")
            .arg(diagram ? diagram->name : m_diagramId, m_outputPath)
            .arg(pixelSize.width())
            .arg(pixelSize.height()),
        m_diagramId);
  }
  resetSurface();
  setBusy(false);
  emit exportSucceeded(outputUrl, pixelSize);
}

void DiagramImageExporter::fail(const QString &message, quint64 serial) {
  if (serial != m_exportSerial)
    return;
  if (m_project)
    m_project->diagnostics()->addError(QStringLiteral("export"), message,
                                       m_diagramId);
  resetSurface();
  setBusy(false);
  emit exportFailed(message);
}

void DiagramImageExporter::resetSurface() {
  if (m_window) {
    m_window->hide();
    m_window->deleteLater();
  }
  m_canvas = nullptr;
  m_window = nullptr;
  m_outputPath.clear();
  m_targetPixelSize = {};
  m_renderedFrames = 0;
  m_captureScheduled = false;
}

void DiagramImageExporter::setBusy(bool busy) {
  if (m_busy == busy)
    return;
  m_busy = busy;
  emit busyChanged();
}

} // namespace yauml
