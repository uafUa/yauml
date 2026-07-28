#include "core/project_controller.h"
#include "core/stereotype_catalog.h"
#include "ui/diagram_canvas.h"
#include "ui/ui_theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QtTest>

#include <algorithm>
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
  canvas->setDiagramId(controller.data().diagrams.first().id);
  canvas->setGridSpacing(20);
  window.show();
  if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
    QVERIFY(QTest::qWaitForWindowExposed(&window));

  const QImage captured = captureWindow(window);
  QVERIFY2(!captured.isNull(), "Qt Quick did not produce a captured frame");
  QVERIFY(captured.width() >= kViewportSize.width());
  QVERIFY(captured.height() >= kViewportSize.height());
  const QImage actual =
      captured.copy(QRect(QPoint(0, 0), kViewportSize));

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

QTEST_MAIN(RenderRegressionTests)

#include "render_regression_tests.moc"
