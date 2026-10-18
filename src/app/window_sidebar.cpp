#include "app/interface_preferences.hpp"
#include "app/window.hpp"
#include <QAccessible>
#include <QAccessibleWidget>
#include <QEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolBar>
#include <algorithm>
#include <cmath>
#include <numeric>
namespace sketchy {
namespace {
// R094.a bounds, in logical pixels at 100% interface text (scaled by interfaceExtent).
constexpr int sidebarMinimum = 240;
constexpr int sidebarMaximum = 720;
constexpr int sidebarStep = 40;
// The workspace (model viewport or sheet canvas) always keeps this many logical pixels beside
// the sidebar. It is not scaled by interface text: it is drawing area, not text. At the
// narrowest layout that shows the side panel (800 px with 200% text and its 156 px tool rail)
// this still leaves the side panel about 240 px.
constexpr int workspaceMinimum = 400;
const char *const sidebarKeys[]{"sidebarTrayWidth", "sidebarTabbedWidth", "sidebarAssistantWidth"};
// The widths before R094.a, used until the user resizes a layout.
int defaultExtent(bool tray, int percent) {
    return tray ? std::min(320, interfaceExtent(248, percent))
                : std::min(420, interfaceExtent(340, percent));
}
// R094.a handle appearance, from the theme: a panel tone a step darker than the Tray, an
// accent tint while hovered or dragged, and a muted dotted grip (>= 3:1 against both fills).
struct SidebarLook {
    QColor fill, active, grip;
    int gripHeight{28};
};
void paintSidebarHandle(QWidget *handle, const SidebarLook &look, bool active) {
    QPainter painter(handle);
    painter.fillRect(handle->rect(), active ? look.active : look.fill);
    // Two 1 px dotted vertical lines, centred.
    const int height = std::min(look.gripHeight, handle->height());
    const int top = (handle->height() - height) / 2;
    const int centre = handle->width() / 2;
    for (const int x : {centre - 2, centre + 1})
        for (int y = top; y < top + height; y += 2)
            painter.fillRect(QRect(x, y, 1, 1), look.grip);
}
class SidebarHandle : public QSplitterHandle {
  public:
    SidebarHandle(Qt::Orientation orientation, QSplitter *parent, const SidebarLook &look,
                  std::function<void()> reset)
        : QSplitterHandle(orientation, parent), look_(look), reset_(std::move(reset)) {
        setCursor(orientation == Qt::Horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
        setAttribute(Qt::WA_Hover);
    }
    bool active() const { return hovered_ || dragging_; }

  protected:
    bool event(QEvent *event) override {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::HoverEnter:
            hovered_ = true;
            update();
            break;
        case QEvent::Leave:
        case QEvent::HoverLeave:
            hovered_ = false;
            update();
            break;
        default:
            break;
        }
        return QSplitterHandle::event(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        dragging_ = event->button() == Qt::LeftButton;
        update();
        QSplitterHandle::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        QSplitterHandle::mouseReleaseEvent(event);
        dragging_ = false;
        update();
    }
    void paintEvent(QPaintEvent *) override { paintSidebarHandle(this, look_, active()); }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton)
            return QSplitterHandle::mouseDoubleClickEvent(event);
        event->accept();
        // Resetting may re-create this handle, so never inside its own event.
        reset_();
    }

  private:
    const SidebarLook &look_;
    std::function<void()> reset_;
    bool hovered_{}, dragging_{};
};
// The narrow layout's overlay sheet keeps the R094.a handle on its left edge (UX_DESIGN §3.3): the
// same look, cursor and accessible splitter; dragging it resizes the sheet, double-clicking
// resets the widths. The window's callbacks run only while `guard` lives (~Window deletes it).
class OverlayHandle : public QWidget {
  public:
    OverlayHandle(QWidget *parent, const SidebarLook &look, QObject *guard,
                  std::function<int()> width, std::function<void(int)> resize,
                  std::function<void()> reset)
        : QWidget(parent), look_(look), guard_(guard), width_(std::move(width)),
          resize_(std::move(resize)), reset_(std::move(reset)) {
        setObjectName("overlayHandle");
        setAccessibleName("Resize sidebar");
        setCursor(Qt::SplitHCursor);
        setAttribute(Qt::WA_Hover);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    }
    bool active() const { return hovered_ || dragging_; }

  protected:
    bool event(QEvent *event) override {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::HoverEnter:
            hovered_ = true;
            update();
            break;
        case QEvent::Leave:
        case QEvent::HoverLeave:
            hovered_ = false;
            update();
            break;
        default:
            break;
        }
        return QWidget::event(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !guard_)
            return QWidget::mousePressEvent(event);
        dragging_ = true;
        start_ = event->globalPosition().x();
        startWidth_ = width_();
        update();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (!dragging_ || !guard_)
            return QWidget::mouseMoveEvent(event);
        // The sheet is anchored on the right: dragging the left edge left widens it.
        resize_(startWidth_ + int(std::lround(start_ - event->globalPosition().x())));
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        dragging_ = false;
        update();
        QWidget::mouseReleaseEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !guard_)
            return QWidget::mouseDoubleClickEvent(event);
        event->accept();
        dragging_ = false;
        reset_();
    }
    void paintEvent(QPaintEvent *) override { paintSidebarHandle(this, look_, active()); }

  private:
    const SidebarLook &look_;
    QPointer<QObject> guard_;
    std::function<int()> width_;
    std::function<void(int)> resize_;
    std::function<void()> reset_;
    bool hovered_{}, dragging_{};
    qreal start_{};
    int startWidth_{};
};
// AT-SPI exposes the handles as splitters with their orientation (a vertical bar between
// side-by-side panes), not as Qt's default "grip", which the bridge reports as unknown.
// QAccessible::Attribute::Orientation is new in Qt 6.11; older Qt gets the role and name only (D-037).
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
class SidebarHandleAccessible : public QAccessibleWidget, public QAccessibleAttributesInterface {
  public:
    explicit SidebarHandleAccessible(QWidget *handle)
        : QAccessibleWidget(handle, QAccessible::Splitter) {}
    void *interface_cast(QAccessible::InterfaceType type) override {
        if (type == QAccessible::AttributesInterface)
            return static_cast<QAccessibleAttributesInterface *>(this);
        return QAccessibleWidget::interface_cast(type);
    }
    QList<QAccessible::Attribute> attributeKeys() const override {
        return {QAccessible::Attribute::Orientation};
    }
    QVariant attributeValue(QAccessible::Attribute key) const override {
        if (key != QAccessible::Attribute::Orientation)
            return {};
        // A horizontal splitter's handle, and the overlay sheet's edge, is a vertical separator.
        const auto *handle = qobject_cast<const QSplitterHandle *>(widget());
        return QVariant::fromValue(
            !handle || handle->orientation() == Qt::Horizontal ? Qt::Vertical : Qt::Horizontal);
    }
};
#else
class SidebarHandleAccessible : public QAccessibleWidget {
  public:
    explicit SidebarHandleAccessible(QWidget *handle)
        : QAccessibleWidget(handle, QAccessible::Splitter) {}
};
#endif
QAccessibleInterface *sidebarHandleAccessible(const QString &, QObject *object) {
    if (auto *handle = dynamic_cast<SidebarHandle *>(object))
        return new SidebarHandleAccessible(handle);
    if (auto *handle = dynamic_cast<OverlayHandle *>(object))
        return new SidebarHandleAccessible(handle);
    return nullptr;
}
class SidebarSplitter : public QSplitter {
  public:
    SidebarSplitter(std::function<void()> reset) : QSplitter(Qt::Horizontal), reset_(reset) {
        static const bool installed = [] {
            QAccessible::installFactory(sidebarHandleAccessible);
            return true;
        }();
        (void)installed;
    }
    const SidebarLook &look() const { return look_; }
    void setLook(const SidebarLook &look) {
        look_ = look;
        for (int index = 0; index < count(); ++index)
            handle(index)->update();
    }

  protected:
    QSplitterHandle *createHandle() override {
        return new SidebarHandle(orientation(), this, look_, reset_);
    }

  private:
    SidebarLook look_;
    std::function<void()> reset_;
};
// Re-fits the sidebar after a column is shown or hidden, or the splitter changes size (for
// example when the Sheets tool rail replaces the wider Model rail).
class SidebarWatcher : public QObject {
  public:
    SidebarWatcher(QObject *parent, std::function<void()> changed)
        : QObject(parent), changed_(std::move(changed)) {}
    bool eventFilter(QObject *object, QEvent *event) override {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::ShowToParent:
        case QEvent::HideToParent:
        case QEvent::Resize:
            changed_();
            break;
        default:
            break;
        }
        return QObject::eventFilter(object, event);
    }

  private:
    std::function<void()> changed_;
};
} // namespace
void Window::setupSidebar(QWidget *workspace) {
    QSettings preferences("SketchyUp", "SketchyUp");
    for (int column = 0; column < 3; ++column) {
        bool valid = false;
        const auto stored = preferences.value(sidebarKeys[column]).toInt(&valid);
        // A missing, corrupt or out-of-range width falls back to the default.
        sidebarWidths_[column] =
            valid && stored >= sidebarMinimum && stored <= sidebarMaximum ? stored : 0;
    }
    auto *splitter = new SidebarSplitter([this] {
        QMetaObject::invokeMethod(this, [this] { resetSidebar(); }, Qt::QueuedConnection);
    });
    splitter->setObjectName("sidebarSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(workspace);
    splitter->addWidget(sideTabs_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    sidebarSplitter_ = splitter;
    content_->addWidget(splitter, 1);
    connect(splitter, &QSplitter::splitterMoved, this, [this](int, int handle) {
        rememberSidebar(handle);
        layoutSidebar();
    });
    auto *watcher = new SidebarWatcher(this, [this] { queueSidebarLayout(); });
    watcher->setObjectName("sidebarWatcher");
    sideTabs_->installEventFilter(watcher);
    assistant_->installEventFilter(watcher);
    splitter->installEventFilter(watcher);
}
void Window::detachSidebar() {
    // Closing the window hides and resizes the sidebar's widgets while ~QWidget runs, after the
    // Window is gone: no callback may reach it from here on. Deleting the watcher removes its
    // event filters.
    delete findChild<QObject *>("sidebarWatcher", Qt::FindDirectChildrenOnly);
    if (sidebarSplitter_)
        disconnect(sidebarSplitter_, nullptr, this, nullptr);
}
void Window::queueSidebarLayout() {
    if (sidebarLayoutQueued_)
        return;
    sidebarLayoutQueued_ = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            sidebarLayoutQueued_ = false;
            layoutSidebar();
        },
        Qt::QueuedConnection);
}
void Window::layoutSidebar() {
    if (!sidebarSplitter_ || !assistant_ || !railStack_)
        return;
    auto *splitter = sidebarSplitter_;
    const auto percent = interfaceTextPercent();
    // The visible sidebar columns, left to right, and the width each one remembers.
    std::vector<std::pair<QWidget *, int>> columns;
    // In the narrow layout the side panel is in the overlay sheet, not a column.
    if (!sideTabs_->isHidden() && splitter->indexOf(sideTabs_) >= 0)
        columns.emplace_back(sideTabs_,
                             sideTabs_->indexOf(assistant_) >= 0 ? SidebarTabbed : SidebarTray);
    if (splitter->indexOf(assistant_) >= 0 && !assistant_->isHidden())
        columns.emplace_back(assistant_, SidebarAssistant);
    const int count = int(columns.size());
    splitter->setHandleWidth(interfaceExtent(6, percent));
    // The splitter's own width once laid out; before that, the window less the tool rail.
    const int rail = std::min(railStack_->currentWidget()->maximumWidth(), width()) +
                     (panelStrip_ && !panelStrip_->isHidden() ? panelStrip_->maximumWidth() : 0);
    const int available = splitter->isVisible() ? splitter->width() : std::max(0, width() - rail);
    const int handles = splitter->handleWidth() * count;
    // Everything beyond the workspace minimum may go to the sidebar.
    const int budget = std::max(0, available - workspaceMinimum - handles);
    const int low = interfaceExtent(sidebarMinimum, percent);
    const int high = interfaceExtent(sidebarMaximum, percent);
    std::vector<int> sizes;
    for (auto [widget, column] : columns) {
        const auto stored = sidebarWidths_[column];
        sizes.push_back(std::clamp(stored ? interfaceExtent(stored, percent)
                                          : defaultExtent(column == SidebarTray, percent),
                                   low, high));
    }
    // Over budget: narrow the rightmost columns to the minimum first, then share what is left.
    int total = std::accumulate(sizes.begin(), sizes.end(), 0);
    for (int i = count - 1; i >= 0 && total > budget; --i) {
        const int cut = std::min(total - budget, sizes[i] - low);
        sizes[i] -= cut;
        total -= cut;
    }
    if (total > budget) {
        int left = budget;
        for (int i = 0; i < count; ++i) {
            const int share = i == count - 1 ? left : sizes[i] * budget / total;
            sizes[i] = share;
            left -= share;
        }
        total = budget;
    }
    // Drag limits: a column never goes below the minimum (or its share when the window cannot
    // fit the minimum) and never pushes the workspace below its minimum.
    const int floor = count ? std::min(low, budget / count) : low;
    for (int i = 0; i < count; ++i) {
        auto *widget = columns[i].first;
        widget->setMinimumWidth(std::min(floor, sizes[i]));
        widget->setMaximumWidth(std::max(sizes[i], std::min(high, budget - (total - sizes[i]))));
    }
    // A hidden side panel, or one in the overlay sheet, drops its column limits; they are
    // recomputed when it is a column again.
    if (sideTabs_->isHidden() || splitter->indexOf(sideTabs_) < 0) {
        sideTabs_->setMinimumWidth(0);
        sideTabs_->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    auto *workspace = splitter->widget(0);
    workspace->setMinimumWidth(std::min(workspaceMinimum, available));
    QList<int> layout;
    for (int index = 0; index < splitter->count(); ++index) {
        auto *widget = splitter->widget(index);
        const auto found = std::find_if(columns.begin(), columns.end(),
                                        [widget](const auto &c) { return c.first == widget; });
        if (widget == workspace)
            layout.push_back(std::max(0, available - handles - total));
        else if (found != columns.end())
            layout.push_back(sizes[found - columns.begin()]);
        else
            layout.push_back(low);
    }
    if (splitter->sizes() != layout)
        splitter->setSizes(layout);
    // Handle i sits before widget i; the first visible column's handle is the sidebar's edge.
    for (int index = 1; index < splitter->count(); ++index) {
        auto *handle = splitter->handle(index);
        const bool edge = count && splitter->widget(index) == columns.front().first;
        handle->setAccessibleName(edge ? "Resize sidebar" : "Resize between Tray and Assistant");
    }
    placeOverlay();
}
void Window::rememberSidebar(int handle) {
    // Handle i moved the boundary between widgets i - 1 and i; remember the sidebar columns.
    const auto sizes = sidebarSplitter_->sizes();
    for (int index : {handle - 1, handle})
        if (index > 0 && index < sidebarSplitter_->count() &&
            !sidebarSplitter_->widget(index)->isHidden())
            storeSidebarWidth(sidebarSplitter_->widget(index), sizes[index]);
}
void Window::storeSidebarWidth(QWidget *widget, int width) {
    storeSidebarColumn(widget == assistant_                  ? SidebarAssistant
                       : sideTabs_->indexOf(assistant_) >= 0 ? SidebarTabbed
                                                             : SidebarTray,
                       width);
}
void Window::storeSidebarColumn(int column, int width) {
    const auto percent = interfaceTextPercent();
    // Stored at 100% text, so a text-size change keeps the width in proportion.
    const int stored =
        std::clamp((width * 100 + percent / 2) / percent, sidebarMinimum, sidebarMaximum);
    sidebarWidths_[column] = stored;
    QSettings("SketchyUp", "SketchyUp").setValue(sidebarKeys[column], stored);
}
void Window::stepSidebar(int direction) {
    // The column at the sidebar's edge: the side panel, or the Assistant column without it.
    QWidget *edge{};
    for (int index = 1; index < sidebarSplitter_->count() && !edge; ++index)
        if (!sidebarSplitter_->widget(index)->isHidden())
            edge = sidebarSplitter_->widget(index);
    const auto step = interfaceExtent(sidebarStep, interfaceTextPercent());
    if (!edge && overlayOpen_ && panelOverlay_) {
        // The narrow layout's overlay sheet steps its own (the tabbed) width.
        resizeOverlay(sideTabs_->width() + direction * step);
        return;
    }
    if (!edge) {
        status_->setText("Show the Model panel or Assistant to resize the sidebar.");
        return;
    }
    const int current = edge->width();
    const int target =
        std::clamp(current + direction * step, edge->minimumWidth(), edge->maximumWidth());
    if (target == current)
        return;
    storeSidebarWidth(edge, target);
    layoutSidebar();
}
void Window::resetSidebar() {
    QSettings preferences("SketchyUp", "SketchyUp");
    for (int column = 0; column < 3; ++column) {
        sidebarWidths_[column] = 0;
        preferences.remove(sidebarKeys[column]);
    }
    layoutSidebar();
}
void Window::themeSidebar(const ThemeColors &colors) {
    if (!sidebarSplitter_)
        return;
    SidebarLook look;
    look.fill = colors.hover;
    // An accent tint over the panel surface.
    const auto tint = [&](qreal amount) {
        return QColor::fromRgbF(
            colors.surface.redF() + (colors.accent.redF() - colors.surface.redF()) * amount,
            colors.surface.greenF() + (colors.accent.greenF() - colors.surface.greenF()) * amount,
            colors.surface.blueF() + (colors.accent.blueF() - colors.surface.blueF()) * amount);
    };
    look.active = tint(0.25);
    look.grip = colors.muted;
    look.gripHeight = interfaceExtent(28, interfaceTextPercent());
    static_cast<SidebarSplitter *>(sidebarSplitter_)->setLook(look);
    if (overlayHandle_)
        overlayHandle_->update();
}
QWidget *Window::createOverlayHandle(QWidget *overlay) {
    return new OverlayHandle(
        overlay, static_cast<SidebarSplitter *>(sidebarSplitter_)->look(), narrowContext_,
        [this] { return sideTabs_->width(); }, [this](int width) { resizeOverlay(width); },
        [this] {
            QMetaObject::invokeMethod(this, [this] { resetSidebar(); }, Qt::QueuedConnection);
        });
}
void Window::placeOverlay() {
    if (!panelOverlay_ || !sidebarSplitter_ || !overlayHandle_)
        return;
    const auto percent = interfaceTextPercent();
    const int handle = interfaceExtent(6, percent);
    overlayHandle_->setFixedWidth(handle);
    const int low = interfaceExtent(sidebarMinimum, percent);
    const int high = interfaceExtent(sidebarMaximum, percent);
    const auto stored = sidebarWidths_[SidebarTabbed];
    int panel = std::clamp(
        stored ? interfaceExtent(stored, percent) : defaultExtent(false, percent), low, high);
    // Anchored to the workspace's right edge and clamped to it, so it never leaves the window.
    const auto area = sidebarSplitter_->geometry();
    panel = std::max(0, std::min(panel, area.width() - handle));
    const QRect geometry(area.right() + 1 - panel - handle, area.top(), panel + handle,
                         area.height());
    if (panelOverlay_->geometry() != geometry)
        panelOverlay_->setGeometry(geometry);
    if (overlayOpen_)
        panelOverlay_->raise();
}
void Window::resizeOverlay(int panelWidth) {
    const auto percent = interfaceTextPercent();
    const int low = interfaceExtent(sidebarMinimum, percent);
    const int high = interfaceExtent(sidebarMaximum, percent);
    const int room = sidebarSplitter_->width() - overlayHandle_->width();
    const int width =
        std::clamp(panelWidth, std::min(low, room), std::max(1, std::min(high, room)));
    if (width == sideTabs_->width())
        return;
    // The sheet is the tabbed side panel: resizing it updates the same setting (R094.a).
    storeSidebarColumn(SidebarTabbed, width);
    placeOverlay();
}
} // namespace sketchy
