#include "core/project_controller.h"
#include "core/stereotype_catalog.h"
#include "ui/diagram_canvas.h"
#include "ui/diagram_document_exporter.h"
#include "ui/diagram_image_exporter.h"
#include "ui/sequence_diagram_layout.h"
#include "ui/ui_theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

#include <algorithm>
#include <array>
#include <cmath>

using namespace yauml;

namespace {

constexpr QSize kViewportSize(760, 500);
constexpr int kSignificantChannelDelta = 18;
// DirectWrite, Qt releases, and counter-scaled mixed-DPI rendering can
// rasterize the same one-pixel grid and glyph outlines differently. The
// changed-pixel allowance absorbs that edge noise; the stricter mean-channel
// limit still rejects geometry or palette movement.
constexpr double kMaximumChangedPixelRatio = 0.04;
constexpr double kMaximumMeanChannelDelta = 0.85;

QString baselineFileName() {
#ifdef Q_OS_WIN
  return QStringLiteral("canonical-diagram-windows.png");
#elif defined(Q_OS_MACOS)
  return QStringLiteral("canonical-diagram-macos.png");
#else
  return QStringLiteral("canonical-diagram-linux.png");
#endif
}

QVariantMap canonicalTypeIconSources() {
  const auto source = [](const char *relativePath) {
    const QString path = QFINDTESTDATA(relativePath);
    return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString();
  };
  return {
      {QStringLiteral("class"), source("../assets/icons/tree-class.svg")},
      {QStringLiteral("struct"), source("../assets/icons/tree-struct.svg")},
      {QStringLiteral("enumeration"), source("../assets/icons/tree-enum.svg")}};
}

const NodePresentation *nodeForElement(const Diagram &diagram,
                                       const QString &elementId) {
  const auto match = std::find_if(diagram.nodes.cbegin(), diagram.nodes.cend(),
                                  [&](const NodePresentation &node) {
                                    return node.elementId == elementId;
                                  });
  return match == diagram.nodes.cend() ? nullptr : &*match;
}

void editType(ProjectController &controller, const QString &elementId,
              const QString &name, const QString &attributes,
              const QString &operations, const QStringList &stereotypes = {}) {
  controller.selectObject(elementId, QStringLiteral("element"));
  controller.setSelectedName(name);
  controller.setSelectedAttributes(attributes);
  controller.setSelectedOperations(operations);
  if (!stereotypes.isEmpty())
    controller.assignStereotypes(QStringLiteral("element"), elementId,
                                 stereotypes);
}

bool populateCanonicalDiagram(ProjectController &controller) {
  controller.newProject(QStringLiteral("Rendering Regression"));
  const QString diagramId = controller.data().diagrams.first().id;

  const QString packageId =
      controller.addElementAt(QStringLiteral("package"), diagramId, 18, 18);
  controller.selectObject(packageId, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("Sales"));
  controller.assignStereotypes(QStringLiteral("element"), packageId,
                               {QStringLiteral("uml.subsystem")});
  const auto &packageFrame =
      controller.data().diagrams.first().containers.last();
  const QVariantMap packageGeometry{{QStringLiteral("id"), packageFrame.id},
                                    {QStringLiteral("x"), 18.0},
                                    {QStringLiteral("y"), 18.0},
                                    {QStringLiteral("width"), 670.0},
                                    {QStringLiteral("height"), 420.0}};
  controller.updatePresentationGeometries(
      diagramId, {packageGeometry},
      QStringLiteral("Position visual regression package"));

  const QString serviceId =
      controller.addElementAt(QStringLiteral("class"), diagramId, 62, 78);
  editType(controller, serviceId, QStringLiteral("OrderService"),
           QStringLiteral("- repository: IOrderRepository\n"
                          "- status: OrderStatus"),
           QStringLiteral("+ place(order: Order): void\n"
                          "+ cancel(id: Id): bool"),
           {stereotype_catalog::kApiStereotypeId});

  const QString repositoryId =
      controller.addElementAt(QStringLiteral("struct"), diagramId, 410, 78);
  editType(controller, repositoryId, QStringLiteral("IOrderRepository"),
           QStringLiteral("+ connection: Connection"),
           QStringLiteral("+ save(order: Order): void\n"
                          "+ find(id: Id): Order"),
           {QStringLiteral("uml.interface")});

  const QString statusId = controller.addElementAt(
      QStringLiteral("enumeration"), diagramId, 430, 292);
  controller.selectObject(statusId, QStringLiteral("element"));
  controller.setSelectedName(QStringLiteral("OrderStatus"));
  controller.setSelectedLiterals(
      QStringLiteral("Pending\nConfirmed\nCancelled"));

  const Diagram &diagramBeforeGeometry = controller.data().diagrams.first();
  const NodePresentation *serviceNode =
      nodeForElement(diagramBeforeGeometry, serviceId);
  const NodePresentation *repositoryNode =
      nodeForElement(diagramBeforeGeometry, repositoryId);
  const NodePresentation *statusNode =
      nodeForElement(diagramBeforeGeometry, statusId);
  if (!serviceNode || !repositoryNode || !statusNode)
    return false;
  controller.updateNodeGeometry(diagramId, serviceNode->id, 62, 78, 238, 146);
  controller.updateNodeGeometry(diagramId, repositoryNode->id, 410, 78, 228,
                                146);
  controller.updateNodeGeometry(diagramId, statusNode->id, 430, 292, 180, 98);

  const Diagram &diagram = controller.data().diagrams.first();
  serviceNode = nodeForElement(diagram, serviceId);
  repositoryNode = nodeForElement(diagram, repositoryId);
  statusNode = nodeForElement(diagram, statusId);
  if (!serviceNode || !repositoryNode || !statusNode)
    return false;

  const QString realizationConnector = controller.createRelationshipWithRouting(
      diagramId, serviceNode->id, repositoryNode->id,
      QStringLiteral("realization"), QStringLiteral("straight"));
  const QString realizationId = controller.selectedId();
  if (realizationConnector.isEmpty() || realizationId.isEmpty())
    return false;
  controller.assignStereotypes(QStringLiteral("relationship"), realizationId,
                               {QStringLiteral("uml.trace")});
  controller.setSelectedSourceRole(QStringLiteral("service"));
  controller.setSelectedSourceMultiplicity(QStringLiteral("1"));
  controller.setSelectedTargetRole(QStringLiteral("repository"));
  controller.setSelectedTargetMultiplicity(QStringLiteral("1"));
  controller.setConnectorAnnotationPlacement(diagramId, realizationConnector,
                                             QStringLiteral("stereotype"), 0.5,
                                             0, -18);

  const QString compositionConnector = controller.createRelationshipWithRouting(
      diagramId, serviceNode->id, statusNode->id, QStringLiteral("composition"),
      QStringLiteral("orthogonal"));
  if (compositionConnector.isEmpty())
    return false;
  controller.setSelectedSourceRole(QStringLiteral("status"));
  controller.setSelectedSourceMultiplicity(QStringLiteral("1"));
  controller.setSelectedTargetMultiplicity(QStringLiteral("1"));
  controller.insertConnectorBendPoint(diagramId, compositionConnector, 0, 346,
                                      260);

  controller.clearSelection();
  return true;
}

QImage captureWindow(QQuickWindow &window) {
  QImage image;
  // Text atlases are populated on the first scene-graph update. Capturing a
  // few complete frames makes the test independent of event-loop scheduling.
  for (int frame = 0; frame < 4; ++frame) {
    window.requestUpdate();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    QTest::qWait(25);
    image = window.grabWindow();
  }
  return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

struct ImageDifference {
  qsizetype changedPixels = 0;
  qsizetype totalPixels = 0;
  double meanChannelDelta = 0.0;
  int maximumChannelDelta = 0;
  QImage visualization;

  double changedPixelRatio() const {
    return totalPixels > 0 ? static_cast<double>(changedPixels) /
                                 static_cast<double>(totalPixels)
                           : 1.0;
  }
};

ImageDifference compareImages(const QImage &expected, const QImage &actual) {
  ImageDifference result;
  if (expected.size() != actual.size())
    return result;

  result.totalPixels = expected.width() * expected.height();
  result.visualization =
      QImage(expected.size(), QImage::Format_ARGB32_Premultiplied);
  quint64 accumulatedChannelDelta = 0;
  for (int y = 0; y < expected.height(); ++y) {
    const auto *expectedLine =
        reinterpret_cast<const QRgb *>(expected.constScanLine(y));
    const auto *actualLine =
        reinterpret_cast<const QRgb *>(actual.constScanLine(y));
    auto *differenceLine =
        reinterpret_cast<QRgb *>(result.visualization.scanLine(y));
    for (int x = 0; x < expected.width(); ++x) {
      const QRgb expectedPixel = expectedLine[x];
      const QRgb actualPixel = actualLine[x];
      const int redDelta = std::abs(qRed(expectedPixel) - qRed(actualPixel));
      const int greenDelta =
          std::abs(qGreen(expectedPixel) - qGreen(actualPixel));
      const int blueDelta = std::abs(qBlue(expectedPixel) - qBlue(actualPixel));
      const int alphaDelta =
          std::abs(qAlpha(expectedPixel) - qAlpha(actualPixel));
      const int maximumDelta =
          std::max({redDelta, greenDelta, blueDelta, alphaDelta});
      accumulatedChannelDelta +=
          static_cast<quint64>(redDelta + greenDelta + blueDelta + alphaDelta);
      result.maximumChannelDelta =
          std::max(result.maximumChannelDelta, maximumDelta);
      if (maximumDelta > kSignificantChannelDelta)
        ++result.changedPixels;

      // Amplification makes subtle antialiasing changes visible in CI
      // artifacts while preserving their channel identity.
      differenceLine[x] =
          qRgba(std::min(255, redDelta * 6), std::min(255, greenDelta * 6),
                std::min(255, blueDelta * 6), 255);
    }
  }
  result.meanChannelDelta = static_cast<double>(accumulatedChannelDelta) /
                            static_cast<double>(result.totalPixels * 4);
  return result;
}

} // namespace

class RenderRegressionTests final : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void canonicalDiagramMatchesBaseline();
  void wideSequenceLabelAtFractionalZoom_data();
  void wideSequenceLabelAtFractionalZoom();
  void activityDiagramExportsRenderedNodesAndFlows();
  void pngExporterCapturesCompleteDiagram();
  void pdfExporterCapturesCompleteDiagram();
  void svgExporterWritesVectorClassDiagram();
  void drawioExporterWritesEditableClassDiagram();
  void vectorExportersSupportSequenceDiagrams();
};

void RenderRegressionTests::initTestCase() {
  // Do not let a developer's saved application theme or geometry preferences
  // influence the reference image.
  QCoreApplication::setOrganizationName(QStringLiteral("yauml-tests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("render-regression-tests"));
  QSettings settings;
  settings.clear();

  QFont font(QStringLiteral("Segoe UI"));
  font.setPixelSize(12);
  font.setHintingPreference(QFont::PreferNoHinting);
  QGuiApplication::setFont(font);

  ui::UiTheme theme;
  theme.resetDefaultColors();
}

void RenderRegressionTests::canonicalDiagramMatchesBaseline() {
  const QString baselinePath = QDir(QStringLiteral(YAUML_RENDER_BASELINE_DIR))
                                   .filePath(baselineFileName());
  const bool updateBaseline =
      qEnvironmentVariableIntValue("YAUML_UPDATE_RENDER_BASELINES") == 1;
  if (!updateBaseline && !QFileInfo::exists(baselinePath)) {
    QSKIP(qPrintable(
        QStringLiteral("No reference image exists for this platform: %1")
            .arg(baselinePath)));
  }

  ProjectController controller;
  QVERIFY(populateCanonicalDiagram(controller));
  const QString diagramId = controller.data().diagrams.first().id;
  // The reference image intentionally includes type icons. Make that
  // per-diagram choice explicit: new projects inherit the application default,
  // which is not part of this headless test fixture and may differ by runner.
  controller.setDiagramDisplayOptions(
      diagramId, {{QStringLiteral("showTypeIcons"), true}});
  QCOMPARE(controller.data().diagrams.first().showTypeIcons,
           std::optional<bool>(true));

  QQuickWindow window;
  window.setTitle(QStringLiteral("yauml render regression"));
  window.setColor(ui::uiPalette().surface);
  if (QScreen *primaryScreen = QGuiApplication::primaryScreen()) {
    window.setScreen(primaryScreen);
    window.setPosition(primaryScreen->availableGeometry().topLeft() +
                       QPoint(32, 32));
  }
  window.resize(kViewportSize);
  auto *canvas = new DiagramCanvas(window.contentItem());
  canvas->setParentItem(window.contentItem());
  canvas->setWidth(kViewportSize.width());
  canvas->setHeight(kViewportSize.height());
  // grabWindow() returns device pixels. Counter-scale the scene so one diagram
  // unit remains one reference pixel on 100%, 125%, and mixed-DPI monitors.
  canvas->setTransformOrigin(QQuickItem::TopLeft);
  canvas->setScale(1.0 / window.devicePixelRatio());
  canvas->setVisible(true);
  canvas->setProject(&controller);
  canvas->setDiagramId(diagramId);
  canvas->setTypeIconSources(canonicalTypeIconSources());
  canvas->setTypeIconsVisible(true);
  canvas->setGridSpacing(20);
  window.show();
  if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
    QVERIFY(QTest::qWaitForWindowExposed(&window));

  const QImage captured = captureWindow(window);
  QVERIFY2(!captured.isNull(), "Qt Quick did not produce a captured frame");
  QVERIFY(captured.width() >= kViewportSize.width());
  QVERIFY(captured.height() >= kViewportSize.height());
  const QImage actual = captured.copy(QRect(QPoint(0, 0), kViewportSize));

  if (updateBaseline) {
    QVERIFY(QDir().mkpath(QFileInfo(baselinePath).absolutePath()));
    QVERIFY2(
        actual.save(baselinePath),
        qPrintable(QStringLiteral("Could not write %1").arg(baselinePath)));
  }

  const QImage expected(baselinePath);
  QVERIFY2(!expected.isNull(),
           qPrintable(QStringLiteral("Could not read %1").arg(baselinePath)));
  QCOMPARE(actual.size(), expected.size());

  const ImageDifference difference = compareImages(
      expected.convertToFormat(QImage::Format_ARGB32_Premultiplied), actual);
  const bool matches =
      difference.changedPixelRatio() <= kMaximumChangedPixelRatio &&
      difference.meanChannelDelta <= kMaximumMeanChannelDelta;
  if (!matches) {
    const QDir artifacts(QStringLiteral(YAUML_RENDER_ARTIFACT_DIR));
    QVERIFY(QDir().mkpath(artifacts.path()));
    const QString expectedArtifact =
        artifacts.filePath(QStringLiteral("canonical-diagram-expected.png"));
    const QString actualArtifact =
        artifacts.filePath(QStringLiteral("canonical-diagram-actual.png"));
    const QString differenceArtifact =
        artifacts.filePath(QStringLiteral("canonical-diagram-difference.png"));
    expected.save(expectedArtifact);
    actual.save(actualArtifact);
    difference.visualization.save(differenceArtifact);
    const QString message =
        QStringLiteral(
            "Rendered diagram differs from its baseline: %1 changed pixels "
            "(%2%), mean channel delta %3, maximum channel delta %4. "
            "Expected, actual, and amplified difference images are in %5")
            .arg(difference.changedPixels)
            .arg(difference.changedPixelRatio() * 100.0, 0, 'f', 3)
            .arg(difference.meanChannelDelta, 0, 'f', 3)
            .arg(difference.maximumChannelDelta)
            .arg(artifacts.path());
    QVERIFY2(matches, qPrintable(message));
  }
}

void RenderRegressionTests::wideSequenceLabelAtFractionalZoom_data() {
  QTest::addColumn<qreal>("zoom");
  QTest::newRow("112 percent") << qreal(1.12);
  QTest::newRow("125 percent") << qreal(1.12 * 1.12);
}

void RenderRegressionTests::wideSequenceLabelAtFractionalZoom() {
  QFETCH(qreal, zoom);
  ProjectController controller;
  const QString diagramId = controller.addSequenceDiagram();
  const auto currentDiagram = [&] {
    return findDiagram(controller.data(), diagramId);
  };
  const QString source = currentDiagram()->sequenceParticipants.first().id;
  const QString target = controller.addSequenceParticipant(
      diagramId, 2904.0, QStringLiteral("Remote participant"));
  // This span places the atlas boundary within the label, well away from
  // either lifeline. Text past the boundary must remain readable without a
  // dark stripe continuing through the otherwise empty part of the label.
  QVERIFY(!controller
               .createSequenceMessage(diagramId, source, target,
                                      QStringLiteral("async"),
                                      QString(30, u'M'), {}, 160.0)
               .isEmpty());
  const auto layout =
      ui::buildSequenceDiagramLayout(controller.data(), *currentDiagram());
  QCOMPARE(layout.messages.size(), 1);
  const auto &message = layout.messages.first();
  QVERIFY(message.labelRect.width() > 2048.0);
  const qreal center = message.labelRect.center().x();
  const qreal halfText =
      QFontMetricsF(QGuiApplication::font()).horizontalAdvance(message.text) /
      2.0;

  const QSize viewport(780, 180);
  const QRectF view(center - halfText - 28.0, message.labelRect.top() - 20.0,
                    viewport.width() / zoom, viewport.height() / zoom);
  QQuickWindow window;
  window.resize(viewport);
  auto *canvas = new DiagramCanvas(window.contentItem());
  canvas->setParentItem(window.contentItem());
  canvas->setWidth(viewport.width());
  canvas->setHeight(viewport.height());
  canvas->setTransformOrigin(QQuickItem::TopLeft);
  canvas->setScale(1.0 / window.devicePixelRatio());
  canvas->setProject(&controller);
  canvas->setDiagramId(diagramId);
  canvas->configureExportViewport(view, zoom);
  window.show();
  if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
    QVERIFY(QTest::qWaitForWindowExposed(&window));
  const QImage rendered = captureWindow(window);
  QVERIFY(!rendered.isNull());

  const auto inkPixels = [&](qreal from, qreal to) {
    const QRect pixels =
        QRectF((from - view.left()) * zoom,
               (message.labelRect.top() - view.top()) * zoom,
               (to - from) * zoom, (message.labelRect.height() - 2.0) * zoom)
            .toAlignedRect()
            .intersected(rendered.rect());
    int ink = 0;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
      for (int x = pixels.left(); x <= pixels.right(); ++x)
        if (qGray(rendered.pixel(x, y)) < 160)
          ++ink;
    return ink;
  };
  QVERIFY(inkPixels(center - halfText, center - 15.0) > 60);
  QVERIFY(inkPixels(center + 15.0, center + halfText) > 60);
  QCOMPARE(inkPixels(center + halfText + 16.0, center + halfText + 140.0), 0);
}

void RenderRegressionTests::activityDiagramExportsRenderedNodesAndFlows() {
  ProjectController controller;
  const QString diagramId = controller.addActivityDiagram();
  const QString initial =
      findDiagram(controller.data(), diagramId)->activity.nodes.first().id;
  const QString action = controller.addActivityNode(
      diagramId, ActivityNodeType::Action, {160.0, 240.0});
  const QString final = controller.addActivityNode(
      diagramId, ActivityNodeType::Final, {160.0, 380.0});
  QVERIFY(!controller.addActivityFlow(diagramId, initial, action).isEmpty());
  QVERIFY(!controller.addActivityFlow(diagramId, action, final).isEmpty());

  QTemporaryDir outputDirectory;
  QVERIFY(outputDirectory.isValid());
  const QString outputPath =
      outputDirectory.filePath(QStringLiteral("activity.png"));
  DiagramImageExporter exporter;
  exporter.setProject(&controller);
  exporter.setDiagramId(diagramId);
  QSignalSpy succeeded(&exporter, &DiagramImageExporter::exportSucceeded);
  QSignalSpy failed(&exporter, &DiagramImageExporter::exportFailed);
  exporter.exportPng(QUrl::fromLocalFile(outputPath));
  QTRY_VERIFY_WITH_TIMEOUT(!succeeded.isEmpty() || !failed.isEmpty(), 20000);
  QVERIFY2(failed.isEmpty(),
           qPrintable(failed.isEmpty() ? QString{}
                                       : failed.first().first().toString()));

  const QImage exported(outputPath);
  QVERIFY(!exported.isNull());
  QVERIFY(exported.height() > 290);
  const auto &palette = ui::uiPalette();
  int activityFillPixels = 0;
  int activityInkPixels = 0;
  for (int y = 0; y < exported.height(); ++y) {
    for (int x = 0; x < exported.width(); ++x) {
      const auto color = exported.pixelColor(x, y);
      activityFillPixels += color == palette.classFill;
      activityInkPixels += color == palette.connector;
    }
  }
  QVERIFY(activityFillPixels > 1000);
  QVERIFY(activityInkPixels > 250);
}

void RenderRegressionTests::pngExporterCapturesCompleteDiagram() {
  ProjectController controller;
  QVERIFY(populateCanonicalDiagram(controller));
  const QString diagramId = controller.data().diagrams.first().id;

  // Put one presentation well outside the ordinary regression viewport. The
  // exported bitmap must use diagram bounds, not the active window dimensions.
  const NodePresentation &distantNode =
      controller.data().diagrams.first().nodes.last();
  controller.updateNodeGeometry(diagramId, distantNode.id, 1100, 640, 180, 98);

  QTemporaryDir outputDirectory;
  QVERIFY(outputDirectory.isValid());
  const QString outputPath =
      outputDirectory.filePath(QStringLiteral("complete-diagram.png"));

  DiagramImageExporter exporter;
  exporter.setProject(&controller);
  exporter.setDiagramId(diagramId);
  QSignalSpy succeeded(&exporter, &DiagramImageExporter::exportSucceeded);
  QSignalSpy failed(&exporter, &DiagramImageExporter::exportFailed);
  exporter.exportPng(QUrl::fromLocalFile(outputPath));

  QTRY_VERIFY_WITH_TIMEOUT(!succeeded.isEmpty() || !failed.isEmpty(), 20000);
  QVERIFY2(failed.isEmpty(),
           qPrintable(failed.isEmpty() ? QString{}
                                       : failed.first().first().toString()));
  QCOMPARE(succeeded.size(), 1);

  const QImage exported(outputPath);
  QVERIFY2(!exported.isNull(), "The PNG exporter did not write a valid image");
  QVERIFY(exported.width() > 1200);
  QVERIFY(exported.height() > 700);
  QCOMPARE(exported.pixelColor(2, 2), ui::uiPalette().surface);
}

void RenderRegressionTests::pdfExporterCapturesCompleteDiagram() {
  ProjectController controller;
  QVERIFY(populateCanonicalDiagram(controller));
  const QString diagramId = controller.data().diagrams.first().id;

  const NodePresentation &distantNode =
      controller.data().diagrams.first().nodes.last();
  controller.updateNodeGeometry(diagramId, distantNode.id, 1100, 640, 180, 98);

  QTemporaryDir outputDirectory;
  QVERIFY(outputDirectory.isValid());
  const QString outputPath =
      outputDirectory.filePath(QStringLiteral("complete-diagram.pdf"));

  DiagramImageExporter exporter;
  exporter.setProject(&controller);
  exporter.setDiagramId(diagramId);
  QSignalSpy succeeded(&exporter, &DiagramImageExporter::exportSucceeded);
  QSignalSpy failed(&exporter, &DiagramImageExporter::exportFailed);
  exporter.exportPdf(QUrl::fromLocalFile(outputPath));

  QTRY_VERIFY_WITH_TIMEOUT(!succeeded.isEmpty() || !failed.isEmpty(), 20000);
  QVERIFY2(failed.isEmpty(),
           qPrintable(failed.isEmpty() ? QString{}
                                       : failed.first().first().toString()));
  QCOMPARE(succeeded.size(), 1);
  const QSize renderSize = succeeded.first().at(1).toSize();
  QVERIFY(renderSize.width() > 1200);
  QVERIFY(renderSize.height() > 700);

  QFile exported(outputPath);
  QVERIFY(exported.open(QIODevice::ReadOnly));
  const QByteArray bytes = exported.readAll();
  QVERIFY(bytes.startsWith("%PDF-"));
  QVERIFY(bytes.trimmed().endsWith("%%EOF"));
  QVERIFY(bytes.size() > 1024);
}

void RenderRegressionTests::svgExporterWritesVectorClassDiagram() {
  ProjectController controller;
  QVERIFY(populateCanonicalDiagram(controller));
  QTemporaryDir outputDirectory;
  QVERIFY(outputDirectory.isValid());
  const QString outputPath =
      outputDirectory.filePath(QStringLiteral("canonical.svg"));

  ui::DiagramDocumentExportOptions options;
  options.typeIconsVisible = true;
  options.typeIconSources = canonicalTypeIconSources();
  const ui::DiagramDocumentExportResult result = ui::exportDiagramDocument(
      controller.data(), controller.data().diagrams.first(), outputPath,
      ui::DiagramDocumentFormat::Svg, options);
  QVERIFY2(result.success, qPrintable(result.error));
  QVERIFY(result.documentSize.width() > 600);
  QVERIFY(result.documentSize.height() > 400);

  QFile exported(outputPath);
  QVERIFY(exported.open(QIODevice::ReadOnly));
  const QByteArray bytes = exported.readAll();
  QVERIFY(bytes.contains("<svg"));
  QVERIFY(bytes.contains("<polyline"));
  QVERIFY(bytes.contains("OrderService"));
  QVERIFY(bytes.contains("data:image/svg+xml;base64"));
  QXmlStreamReader xml(bytes);
  while (!xml.atEnd())
    xml.readNext();
  QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
}

void RenderRegressionTests::drawioExporterWritesEditableClassDiagram() {
  ProjectController controller;
  QVERIFY(populateCanonicalDiagram(controller));
  QTemporaryDir outputDirectory;
  QVERIFY(outputDirectory.isValid());
  const QString outputPath =
      outputDirectory.filePath(QStringLiteral("canonical.drawio"));

  const ui::DiagramDocumentExportResult result = ui::exportDiagramDocument(
      controller.data(), controller.data().diagrams.first(), outputPath,
      ui::DiagramDocumentFormat::Drawio);
  QVERIFY2(result.success, qPrintable(result.error));

  QFile exported(outputPath);
  QVERIFY(exported.open(QIODevice::ReadOnly));
  const QByteArray bytes = exported.readAll();
  QVERIFY(bytes.contains("<mxfile"));
  QVERIFY(bytes.contains("<mxGraphModel"));
  QVERIFY(bytes.contains("OrderService"));
  QVERIFY(bytes.contains("edge=\"1\""));
  QVERIFY(bytes.contains("yaumlId="));
  QVERIFY(bytes.contains("swimlane;"));
  QVERIFY(bytes.contains("childLayout=stackLayout"));
  QXmlStreamReader xml(bytes);
  QString umlClassCellId;
  bool foundEditableMember = false;
  while (!xml.atEnd()) {
    xml.readNext();
    if (!xml.isStartElement() || xml.name() != QStringLiteral("mxCell"))
      continue;
    const auto attributes = xml.attributes();
    const QString style = attributes.value(QStringLiteral("style")).toString();
    if (style.startsWith(QStringLiteral("swimlane;")) &&
        attributes.hasAttribute(QStringLiteral("yaumlId"))) {
      umlClassCellId = attributes.value(QStringLiteral("id")).toString();
    } else if (!umlClassCellId.isEmpty() &&
               attributes.value(QStringLiteral("parent")) == umlClassCellId &&
               style.startsWith(QStringLiteral("text;"))) {
      foundEditableMember = true;
    }
  }
  QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
  QVERIFY2(!umlClassCellId.isEmpty(),
           "The Draw.io export must contain a native UML class container");
  QVERIFY2(foundEditableMember,
           "UML attributes and operations must be editable child cells");
}

void RenderRegressionTests::vectorExportersSupportSequenceDiagrams() {
  ProjectData project = createStarterProject(QStringLiteral("Sequence export"));
  Diagram sequence;
  sequence.id = newId();
  sequence.name = QStringLiteral("Request flow");
  sequence.type = DiagramType::Sequence;
  const QString callerId = newId();
  const QString serviceId = newId();
  sequence.sequenceParticipants = {{callerId,
                                    QStringLiteral("caller"),
                                    QStringLiteral("Caller"),
                                    {},
                                    {},
                                    {20.0, 20.0, 180.0, 60.0},
                                    {}},
                                   {serviceId,
                                    QStringLiteral("service"),
                                    QStringLiteral("Service"),
                                    {},
                                    {},
                                    {360.0, 20.0, 180.0, 60.0},
                                    {}}};
  SequenceMessage message;
  message.id = newId();
  message.key = QStringLiteral("submit");
  message.sourceParticipantId = callerId;
  message.targetParticipantId = serviceId;
  message.type = SequenceMessageType::Synchronous;
  message.text = QStringLiteral("submit(request)");
  message.y = 150.0;
  sequence.sequenceMessages.append(message);
  const QString retryId = newId();
  sequence.sequenceMessages.append({retryId, QStringLiteral("retry"), serviceId,
                                    serviceId, SequenceMessageType::Asynchronous,
                                    QStringLiteral("retry()"), 220.0, {}, {}});
  sequence.sequenceCombinedFragments.append(
      {newId(), QStringLiteral("retry-if-needed"),
       SequenceCombinedFragmentType::Optional, {serviceId}, retryId, retryId,
       {{retryId, QStringLiteral("failed"), {}}}, {}});
  sequence.sequenceActivations.append({newId(),
                                       QStringLiteral("service-activation"),
                                       serviceId,
                                       {},
                                       message.id,
                                       {},
                                       std::nullopt,
                                       250.0,
                                       QStringLiteral("handle request"),
                                       {}});
  project.diagrams.append(sequence);

  QTemporaryDir outputDirectory;
  QVERIFY(outputDirectory.isValid());
  for (const auto &[format, suffix] :
       std::array<std::pair<ui::DiagramDocumentFormat, QString>, 2>{
           std::pair{ui::DiagramDocumentFormat::Svg, QStringLiteral("svg")},
           std::pair{ui::DiagramDocumentFormat::Drawio,
                     QStringLiteral("drawio")}}) {
    const QString path =
        outputDirectory.filePath(QStringLiteral("sequence.") + suffix);
    const ui::DiagramDocumentExportResult result = ui::exportDiagramDocument(
        project, project.diagrams.last(), path, format);
    QVERIFY2(result.success, qPrintable(result.error));
    QFile exported(path);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    const QByteArray bytes = exported.readAll();
    QVERIFY(bytes.contains("submit(request)"));
    QVERIFY(bytes.contains("opt [failed]"));
    QVERIFY(bytes.contains("retry()"));
    QVERIFY(bytes.contains("Caller"));
    QVERIFY(bytes.contains("Service"));
  }
}

QTEST_MAIN(RenderRegressionTests)

#include "render_regression_tests.moc"
