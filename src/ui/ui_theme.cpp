#include "ui/ui_theme.h"

#include <QReadWriteLock>
#include <QSettings>
#include <array>

namespace uuml::ui {
namespace {

constexpr auto kSettingsGroup = "preferences/theme/colors";

struct ColorRoleDescriptor {
  const char *key;
  const char *label;
  const char *group;
  QColor UiPalette::*member;
};

constexpr std::array kColorRoles = {
    ColorRoleDescriptor{"accent", "Accent", "Application chrome",
                        &UiPalette::accent},
    ColorRoleDescriptor{"surface", "Surface", "Application chrome",
                        &UiPalette::surface},
    ColorRoleDescriptor{"windowBackground", "Window background",
                        "Application chrome", &UiPalette::windowBackground},
    ColorRoleDescriptor{"panelHeader", "Panel header", "Application chrome",
                        &UiPalette::panelHeader},
    ColorRoleDescriptor{"hoverBackground", "Hover background",
                        "Application chrome", &UiPalette::hoverBackground},
    ColorRoleDescriptor{"controlBorder", "Control border", "Application chrome",
                        &UiPalette::controlBorder},
    ColorRoleDescriptor{"overlayBorder", "Overlay border", "Application chrome",
                        &UiPalette::overlayBorder},

    ColorRoleDescriptor{"bodyText", "Body text", "Text", &UiPalette::bodyText},
    ColorRoleDescriptor{"nodeTitleText", "Diagram title text", "Text",
                        &UiPalette::nodeTitleText},
    ColorRoleDescriptor{"mutedText", "Muted text", "Text",
                        &UiPalette::mutedText},
    ColorRoleDescriptor{"emptyStateText", "Empty-state text", "Text",
                        &UiPalette::emptyStateText},
    ColorRoleDescriptor{"zoomText", "Zoom indicator text", "Text",
                        &UiPalette::zoomText},

    ColorRoleDescriptor{"tabStrip", "Tab strip", "Diagram tabs",
                        &UiPalette::tabStrip},
    ColorRoleDescriptor{"tabStripBorder", "Tab strip border", "Diagram tabs",
                        &UiPalette::tabStripBorder},
    ColorRoleDescriptor{"activeTab", "Active tab", "Diagram tabs",
                        &UiPalette::activeTab},
    ColorRoleDescriptor{"inactiveTab", "Inactive tab", "Diagram tabs",
                        &UiPalette::inactiveTab},

    ColorRoleDescriptor{"badgeBackground", "Badge background",
                        "Feedback and editors", &UiPalette::badgeBackground},
    ColorRoleDescriptor{"badgeBorder", "Badge border", "Feedback and editors",
                        &UiPalette::badgeBorder},
    ColorRoleDescriptor{"warningBackground", "Warning background",
                        "Feedback and editors", &UiPalette::warningBackground},
    ColorRoleDescriptor{"warningBorder", "Warning border",
                        "Feedback and editors", &UiPalette::warningBorder},
    ColorRoleDescriptor{"editorBackground", "In-place editor background",
                        "Feedback and editors", &UiPalette::editorBackground},
    ColorRoleDescriptor{"errorRow", "Error log row", "Feedback and editors",
                        &UiPalette::errorRow},
    ColorRoleDescriptor{"warningRow", "Warning log row", "Feedback and editors",
                        &UiPalette::warningRow},
    ColorRoleDescriptor{"alternateRow", "Alternate log row",
                        "Feedback and editors", &UiPalette::alternateRow},

    ColorRoleDescriptor{"canvasGrid", "Canvas grid", "Diagram",
                        &UiPalette::canvasGrid},
    ColorRoleDescriptor{"alignmentGuide", "Alignment guide", "Diagram",
                        &UiPalette::alignmentGuide},
    ColorRoleDescriptor{"selectionOverlay", "Selection overlay", "Diagram",
                        &UiPalette::selectionOverlay},
    ColorRoleDescriptor{"connector", "Connector", "Diagram",
                        &UiPalette::connector},
    ColorRoleDescriptor{"nodeBorder", "Element border", "Diagram",
                        &UiPalette::nodeBorder},
    ColorRoleDescriptor{"compartmentLine", "Header divider", "Diagram",
                        &UiPalette::compartmentLine},
    ColorRoleDescriptor{"compartmentDivider", "Compartment divider", "Diagram",
                        &UiPalette::compartmentDivider},
    ColorRoleDescriptor{"activeHandleFill", "Active handle fill", "Diagram",
                        &UiPalette::activeHandleFill},
    ColorRoleDescriptor{"packageFill", "Package fill", "Diagram",
                        &UiPalette::packageFill},
    ColorRoleDescriptor{"classFill", "Class fill", "Diagram",
                        &UiPalette::classFill},
    ColorRoleDescriptor{"structFill", "Struct fill", "Diagram",
                        &UiPalette::structFill},
    ColorRoleDescriptor{"enumerationFill", "Enumeration fill", "Diagram",
                        &UiPalette::enumerationFill},

    ColorRoleDescriptor{"dragGhostBorder", "Tab-drag border", "Drag and drop",
                        &UiPalette::dragGhostBorder},
    ColorRoleDescriptor{"dragGhostFill", "Tab-drag fill", "Drag and drop",
                        &UiPalette::dragGhostFill},
    ColorRoleDescriptor{"dragGhostText", "Tab-drag text", "Drag and drop",
                        &UiPalette::dragGhostText},
};

UiPalette makeDefaultPalette() {
  // This is the only source-code location containing product color literals.
  UiPalette value;
  value.accent = QColor(QStringLiteral("#1769d2"));
  value.surface = QColor(QStringLiteral("#ffffff"));
  value.windowBackground = QColor(QStringLiteral("#f4f6f8"));
  value.panelHeader = QColor(QStringLiteral("#e8edf2"));
  value.hoverBackground = QColor(QStringLiteral("#edf3f8"));
  value.controlBorder = QColor(QStringLiteral("#b9c4ce"));
  value.overlayBorder = QColor(QStringLiteral("#8f9ba6"));

  value.bodyText = QColor(QStringLiteral("#263238"));
  value.nodeTitleText = QColor(QStringLiteral("#18212a"));
  value.mutedText = QColor(QStringLiteral("#64717d"));
  value.emptyStateText = QColor(QStringLiteral("#687684"));
  value.zoomText = QColor(QStringLiteral("#425466"));

  value.tabStrip = QColor(QStringLiteral("#e7ecf1"));
  value.tabStripBorder = QColor(QStringLiteral("#c5ced7"));
  value.activeTab = QColor(QStringLiteral("#d9eaff"));
  value.inactiveTab = QColor(QStringLiteral("#dfe5eb"));

  value.badgeBackground = QColor(QStringLiteral("#eaf0f5"));
  value.badgeBorder = QColor(QStringLiteral("#c6d0da"));
  value.warningBackground = QColor(QStringLiteral("#fff4ce"));
  value.warningBorder = QColor(QStringLiteral("#c89b25"));
  value.editorBackground = QColor(QStringLiteral("#ffffe8"));
  value.errorRow = QColor(QStringLiteral("#fff0f0"));
  value.warningRow = QColor(QStringLiteral("#fff8e5"));
  value.alternateRow = QColor(QStringLiteral("#f7f9fb"));

  value.canvasGrid = QColor(QStringLiteral("#dce2e8"));
  value.alignmentGuide = QColor(QStringLiteral("#d23c8e"));
  value.selectionOverlay = QColor(23, 105, 210, 28);
  value.connector = QColor(QStringLiteral("#52606d"));
  value.nodeBorder = QColor(QStringLiteral("#3f4b56"));
  value.compartmentLine = QColor(QStringLiteral("#65727e"));
  value.compartmentDivider = QColor(QStringLiteral("#c5ccd3"));
  value.activeHandleFill = QColor(QStringLiteral("#9dceff"));
  value.packageFill = QColor(QStringLiteral("#fff1c2"));
  value.classFill = QColor(QStringLiteral("#f8fbff"));
  value.structFill = QColor(QStringLiteral("#eefaf1"));
  value.enumerationFill = QColor(QStringLiteral("#f7efff"));

  value.dragGhostBorder = QColor(QStringLiteral("#8aa9cc"));
  value.dragGhostFill = QColor(QStringLiteral("#e2efff"));
  value.dragGhostText = QColor(QStringLiteral("#203548"));
  return value;
}

const UiPalette &defaultPalette() {
  static const UiPalette palette = makeDefaultPalette();
  return palette;
}

struct PaletteState {
  QReadWriteLock lock;
  UiPalette palette = defaultPalette();
};

PaletteState &paletteState() {
  // Construction-on-first-use avoids static initialization order dependencies
  // with QGuiApplication and keeps render-thread access synchronized.
  static PaletteState state;
  return state;
}

const ColorRoleDescriptor *descriptorFor(const QString &role) {
  for (const auto &descriptor : kColorRoles) {
    if (role == QLatin1String(descriptor.key))
      return &descriptor;
  }
  return nullptr;
}

QColor colorFromVariant(const QVariant &value) {
  if (value.canConvert<QColor>()) {
    const QColor converted = value.value<QColor>();
    if (converted.isValid())
      return converted;
  }
  return QColor(value.toString());
}

QString serializedColor(const QColor &color) {
  const auto format = color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb;
  return color.name(format).toUpper();
}

} // namespace

UiPalette uiPalette() {
  auto &state = paletteState();
  QReadLocker locker(&state.lock);
  return state.palette;
}

UiTheme::UiTheme(QObject *parent) : QObject(parent) { load(); }

#define UUML_THEME_GETTER(name)                                                \
  QColor UiTheme::name() const { return uiPalette().name; }

UUML_THEME_GETTER(accent)
UUML_THEME_GETTER(surface)
UUML_THEME_GETTER(windowBackground)
UUML_THEME_GETTER(panelHeader)
UUML_THEME_GETTER(hoverBackground)
UUML_THEME_GETTER(controlBorder)
UUML_THEME_GETTER(overlayBorder)
UUML_THEME_GETTER(bodyText)
UUML_THEME_GETTER(nodeTitleText)
UUML_THEME_GETTER(mutedText)
UUML_THEME_GETTER(emptyStateText)
UUML_THEME_GETTER(zoomText)
UUML_THEME_GETTER(tabStrip)
UUML_THEME_GETTER(tabStripBorder)
UUML_THEME_GETTER(activeTab)
UUML_THEME_GETTER(inactiveTab)
UUML_THEME_GETTER(badgeBackground)
UUML_THEME_GETTER(badgeBorder)
UUML_THEME_GETTER(warningBackground)
UUML_THEME_GETTER(warningBorder)
UUML_THEME_GETTER(editorBackground)
UUML_THEME_GETTER(errorRow)
UUML_THEME_GETTER(warningRow)
UUML_THEME_GETTER(alternateRow)
UUML_THEME_GETTER(canvasGrid)
UUML_THEME_GETTER(alignmentGuide)
UUML_THEME_GETTER(selectionOverlay)
UUML_THEME_GETTER(connector)
UUML_THEME_GETTER(nodeBorder)
UUML_THEME_GETTER(compartmentLine)
UUML_THEME_GETTER(compartmentDivider)
UUML_THEME_GETTER(activeHandleFill)
UUML_THEME_GETTER(packageFill)
UUML_THEME_GETTER(classFill)
UUML_THEME_GETTER(structFill)
UUML_THEME_GETTER(enumerationFill)
UUML_THEME_GETTER(dragGhostBorder)
UUML_THEME_GETTER(dragGhostFill)
UUML_THEME_GETTER(dragGhostText)

#undef UUML_THEME_GETTER

QVariantList UiTheme::colorRoles() const {
  QVariantList roles;
  roles.reserve(kColorRoles.size());
  for (const auto &descriptor : kColorRoles) {
    QVariantMap role;
    role.insert(QStringLiteral("key"), QLatin1String(descriptor.key));
    role.insert(QStringLiteral("label"), tr(descriptor.label));
    role.insert(QStringLiteral("group"), tr(descriptor.group));
    roles.append(role);
  }
  return roles;
}

QColor UiTheme::color(const QString &role) const {
  const auto *descriptor = descriptorFor(role);
  return descriptor ? uiPalette().*(descriptor->member) : QColor();
}

QColor UiTheme::defaultColor(const QString &role) const {
  const auto *descriptor = descriptorFor(role);
  return descriptor ? defaultPalette().*(descriptor->member) : QColor();
}

QString UiTheme::colorText(const QColor &color) const {
  return color.isValid() ? serializedColor(color) : QString();
}

QString UiTheme::normalizeColor(const QString &text) const {
  const QColor color(text.trimmed());
  return color.isValid() ? serializedColor(color) : QString();
}

void UiTheme::setColor(const QString &role, const QColor &color) {
  QVariantMap colors;
  colors.insert(role, color);
  setColors(colors);
}

void UiTheme::setColors(const QVariantMap &colors) {
  UiPalette next = uiPalette();
  QStringList changedRoles;
  for (const auto &descriptor : kColorRoles) {
    const QString role = QLatin1String(descriptor.key);
    const auto value = colors.constFind(role);
    if (value == colors.cend())
      continue;
    const QColor candidate = colorFromVariant(*value);
    QColor &current = next.*(descriptor.member);
    if (!candidate.isValid() || candidate == current)
      continue;
    current = candidate;
    changedRoles.append(role);
  }
  if (changedRoles.isEmpty())
    return;

  auto &state = paletteState();
  {
    QWriteLocker locker(&state.lock);
    state.palette = next;
  }
  persist(changedRoles);
  emit paletteChanged();
}

void UiTheme::resetDefaultColors() {
  auto &state = paletteState();
  {
    QWriteLocker locker(&state.lock);
    state.palette = defaultPalette();
  }
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.remove(QString());
  settings.endGroup();
  settings.sync();
  emit paletteChanged();
}

void UiTheme::load() {
  UiPalette loaded = defaultPalette();
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  for (const auto &descriptor : kColorRoles) {
    const QString key = QLatin1String(descriptor.key);
    if (!settings.contains(key))
      continue;
    const QColor candidate(settings.value(key).toString());
    if (candidate.isValid())
      loaded.*(descriptor.member) = candidate;
  }
  settings.endGroup();

  auto &state = paletteState();
  QWriteLocker locker(&state.lock);
  state.palette = loaded;
}

void UiTheme::persist(const QStringList &roles) const {
  const UiPalette snapshot = uiPalette();
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  for (const auto &role : roles) {
    const auto *descriptor = descriptorFor(role);
    if (!descriptor)
      continue;
    const QColor current = snapshot.*(descriptor->member);
    const QColor defaultValue = defaultPalette().*(descriptor->member);
    if (current == defaultValue)
      settings.remove(role);
    else
      settings.setValue(role, serializedColor(current));
  }
  settings.endGroup();
  settings.sync();
}

} // namespace uuml::ui
