#include "liquidglass.h"
#include "qwayland-applestia-glass-shapes-v1.h"
#include <QtWaylandClient/QWaylandClientExtension>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#include <QtWaylandClient/private/qwaylandintegration_p.h>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtQuick/private/qquickitem_p.h>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QFileSystemWatcher>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

namespace {
class Extension : public QWaylandClientExtensionTemplate<Extension, &QtWayland::applestia_glass_shapes_manager_v1::destroy>, public QtWayland::applestia_glass_shapes_manager_v1 {
public:
    Extension() : QWaylandClientExtensionTemplate(1) { initialize(); }
    bool usable = true;
    std::function<void()> changed;
    void bind(wl_registry *r, int id, int v) override {
        usable = true;
        QWaylandClientExtensionTemplate::bind(r, id, v);
        if (changed) changed();
    }
    void applestia_glass_shapes_manager_v1_finished() override {
        usable = false;
        if (changed) changed();
    }
};
class Transport : public QObject {
    Q_OBJECT
public:
    std::unique_ptr<Extension> manager;
    QFileSystemWatcher watcher;
    QString marker;
    bool ready = false;
    bool overflowLatched = false;
#ifdef APPLESTIA_GLASS_TEST_HOOKS
    std::optional<bool> testActive;
#endif
    Transport() {
        marker = qEnvironmentVariable("XDG_RUNTIME_DIR") + "/applestia-shapes-" + qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE") + ".ready";
        const QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (!dir.isEmpty()) watcher.addPath(dir);
        connect(&watcher, &QFileSystemWatcher::directoryChanged, this, [this] { refresh(); });
        connect(&watcher, &QFileSystemWatcher::fileChanged, this, [this] { refresh(); });
        if (QGuiApplication::platformName().startsWith("wayland")) {
            manager = std::make_unique<Extension>();
            manager->changed = [this] { notify(); };
            connect(manager.get(), &Extension::activeChanged, this, [this] { notify(); });
            // Qt tracks actual registry removal/reappearance. finished additionally
            // makes an old generation unusable before registry removal arrives.
        }
        refresh();
    }
    bool active() const {
#ifdef APPLESTIA_GLASS_TEST_HOOKS
        if (testActive) return *testActive;
#endif
        return manager && manager->isActive() && manager->usable;
    }
    bool localFallback() const { return active() && ready && overflowLatched; }
    bool available() const { return active() && ready && !overflowLatched; }
    void disableForOverflow() {
        if (overflowLatched) return;
        // Process-wide and intentionally never reset, even if markers/windows
        // disappear or the manager rebinds. Only restarting the shell retries.
        overflowLatched = true;
        qWarning("Applestia.Glass: more than 128 visible glass materials in one window; native glass disabled locally until shell restart (using legacy fallback).");
        notify();
    }
    void notify() { emit availabilityChanged(); }
    void refresh() {
        QFile f(marker);
        bool value = f.open(QIODevice::ReadOnly) && f.readAll().trimmed() == "1";
        if (f.exists() && !watcher.files().contains(marker)) watcher.addPath(marker);
        if (ready != value) { ready = value; notify(); }
    }
signals:
    void availabilityChanged();
};
Transport &transport() { static Transport *t = new Transport; return *t; }
struct Frame {
    QMutex mutex;
    QVector<GlassShape> snapshot;
    QVector<GlassShape> rendering;
    QtWayland::applestia_glass_shapes_v1 hints;
    wl_surface *surface = nullptr;
    void release() {
        if (hints.isInitialized()) hints.destroy();
        surface = nullptr;
    }
    ~Frame() { release(); }
};
constexpr auto watchedChanges = QQuickItemPrivate::Geometry | QQuickItemPrivate::SiblingOrder
    | QQuickItemPrivate::Visibility | QQuickItemPrivate::Opacity | QQuickItemPrivate::Destroyed
    | QQuickItemPrivate::Parent | QQuickItemPrivate::Rotation | QQuickItemPrivate::Scale | QQuickItemPrivate::Matrix;
class Registry : public QObject, public QQuickItemChangeListener {
public:
    QQuickWindow *window;
    QList<LiquidGlass*> items;
    QSet<QQuickItem*> watched;
    QList<QMetaObject::Connection> properties;
    std::shared_ptr<Frame> frame = std::make_shared<Frame>();
    explicit Registry(QQuickWindow *w) : QObject(w), window(w) {
        connect(w, &QQuickWindow::afterAnimating, this, [this] { collect(); }, Qt::DirectConnection);
        // Synchronization blocks the GUI thread in Qt's threaded render loop.
        // Latch values here so a subsequent GUI animation cannot overwrite the
        // snapshot used by this render/swap, even if rendering is slow.
        connect(w, &QQuickWindow::beforeSynchronizing, w, [f = frame] {
            QMutexLocker lock(&f->mutex);
            f->rendering = f->snapshot;
        }, Qt::DirectConnection);
        // Direct render-thread callback, never queued to GUI. Only immutable value
        // data and Wayland proxy state are read here, never QML/QObject properties.
        connect(w, &QQuickWindow::beforeRendering, w, [f = frame] {
            QMutexLocker lock(&f->mutex);
            if (!f->hints.isInitialized()) return;
            f->hints.begin();
            auto fixed = [](qreal v) { return wl_fixed_from_double(std::clamp(v, -1000000., 1000000.)); };
            for (const auto &s : f->rendering) {
                f->hints.add_shape(fixed(s.box.x()), fixed(s.box.y()), fixed(s.box.width()), fixed(s.box.height()),
                    fixed(s.radii[0]), fixed(s.radii[1]), fixed(s.radii[2]), fixed(s.radii[3]), s.depth, s.tint, s.preset);
                f->hints.set_clip(fixed(s.clip.x()), fixed(s.clip.y()), fixed(s.clip.width()), fixed(s.clip.height()));
                f->hints.set_opacity(fixed(s.opacity));
            }
        }, Qt::DirectConnection);
        connect(w, &QQuickWindow::widthChanged, this, [this] { window->update(); });
        connect(w, &QQuickWindow::heightChanged, this, [this] { window->update(); });
        connect(&transport(), &Transport::availabilityChanged, this, [this] {
            // Local overflow can restore QML while another native frame renders.
            // Real protocol/marker loss still invalidates immediately as before.
            if (!transport().localFallback()) invalidate();
            window->update();
        });
    }
    ~Registry() override { unwatch(); invalidate(); }
    void invalidate() { QMutexLocker lock(&frame->mutex); frame->release(); }
    void unwatch() {
        for (auto *i : std::as_const(watched)) QQuickItemPrivate::get(i)->removeItemChangeListener(this, watchedChanges);
        watched.clear();
        for (const auto &c : properties) disconnect(c);
        properties.clear();
    }
    void watch() {
        unwatch();
        for (auto *m : items) {
            for (auto *i = m->sourceItem(); i; i = i->parentItem()) {
                if (watched.contains(i)) continue;
                watched.insert(i);
                QQuickItemPrivate::get(i)->addItemChangeListener(this, watchedChanges);
                properties.append(connect(i, &QQuickItem::clipChanged, this, [this] { window->update(); }));
            }
        }
    }
    void prepareSnapshot() {
        bool overflow = false;
        auto shapes = LiquidGlass::collect(window, items, &overflow);
        if (overflow && transport().available()) transport().disableForOverflow();
        // notify() may synchronously remove all native QML Loader markers. Do
        // not use their pointers after notification, nor publish a truncated list.
        QMutexLocker lock(&frame->mutex);
        frame->snapshot = transport().overflowLatched ? QVector<GlassShape>{} : std::move(shapes);
    }
    void collect() {
        watch();
        prepareSnapshot();
        auto *native = dynamic_cast<QtWaylandClient::QWaylandWindow*>(window->handle());
        auto *surface = native ? native->wlSurface() : nullptr;
        if (native && !native->property("applestiaLifecycleWatched").toBool()) {
            native->setProperty("applestiaLifecycleWatched", true);
            connect(native, &QtWaylandClient::QWaylandWindow::wlSurfaceDestroyed, this, [this] { invalidate(); }, Qt::DirectConnection);
            connect(native, &QtWaylandClient::QWaylandWindow::wlSurfaceCreated, this, [this] { window->update(); });
        }
        QMutexLocker lock(&frame->mutex);
        if (transport().localFallback()) {
            // No new association while latched. Retain this surface's existing
            // association and publish empty lists with every fallback frame.
            // Frame submission/end is not applied-commit acknowledgment: a
            // fence/FIFO/timer may still hold the old native foreground buffer.
            // Only surface/window destruction or capability loss releases it.
            if (!surface || frame->surface != surface) frame->release();
            return;
        }
        if (!transport().available() || !surface) { frame->release(); return; }
        if (frame->surface != surface) {
            frame->release();
            frame->hints.init(transport().manager->get_shapes(surface));
            frame->surface = surface;
        }
    }
    void changed() { window->update(); }
    void itemGeometryChanged(QQuickItem*, QQuickGeometryChange, const QRectF&) override { changed(); }
    void itemSiblingOrderChanged(QQuickItem*) override { changed(); }
    void itemVisibilityChanged(QQuickItem*) override { changed(); }
    void itemOpacityChanged(QQuickItem*) override { changed(); }
    void itemParentChanged(QQuickItem*, QQuickItem*) override { changed(); }
    void itemRotationChanged(QQuickItem*) override { changed(); }
    void itemScaleChanged(QQuickItem*) override { changed(); }
    void itemTransformChanged(QQuickItem*, QQuickItem*) override { changed(); }
    void itemDestroyed(QQuickItem *i) override { watched.remove(i); changed(); }
};
QHash<QQuickWindow*, Registry*> registries;
Registry *registry(QQuickWindow *w) {
    if (!w) return nullptr;
    if (!registries.contains(w)) {
        auto *r = new Registry(w);
        registries.insert(w, r);
        QObject::connect(w, &QObject::destroyed, [w] { registries.remove(w); });
    }
    return registries.value(w);
}
void detach(LiquidGlass *m) {
    for (auto *r : std::as_const(registries)) if (r->items.removeAll(m)) {
        r->watch(); r->window->update();
        // Keep the committed shapes paired with Qt's still-committed foreground
        // buffer. afterAnimating prepares the remaining (possibly empty) list;
        // beforeRendering replaces pending hints for the next real Qt commit.
    }
}
}

#ifdef APPLESTIA_GLASS_TEST_HOOKS
#include "client-test-hooks.h"
void GlassClientTest::attachAssociation(QQuickWindow *window, applestia_glass_shapes_v1 *proxy) {
    auto *r = registry(window);
    QMutexLocker lock(&r->frame->mutex);
    r->frame->hints.init(proxy);
}
applestia_glass_shapes_v1 *GlassClientTest::association(QQuickWindow *window) {
    auto *r = registry(window);
    QMutexLocker lock(&r->frame->mutex);
    return r->frame->hints.object();
}
bool GlassClientTest::publishFrame(QQuickWindow *window) {
    auto *r = registry(window);
    r->prepareSnapshot();
    // Offscreen tests have no QtWayland wl_surface. Exercise the real value
    // handoff/request callbacks without the native-surface acquisition step.
    return QMetaObject::invokeMethod(window, "beforeSynchronizing", Qt::DirectConnection)
        && QMetaObject::invokeMethod(window, "beforeRendering", Qt::DirectConnection);
}
namespace GlassClientTest {
void prepareFrame(QQuickWindow *window) { registry(window)->prepareSnapshot(); }
void setCapability(bool active, bool markerReady) {
    auto &t = transport();
    t.testActive = active;
    t.ready = markerReady;
    t.notify();
}
}
#endif

LiquidGlass::LiquidGlass(QQuickItem *p) : QQuickItem(p) {
    connect(this, &LiquidGlass::materialChanged, this, [this] {
        if (auto *r = registry(window())) { r->watch(); window()->update(); }
    });
    connect(this, &QQuickItem::enabledChanged, this, [this] { if (window()) window()->update(); });
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *w) {
        detach(this);
        if (auto *r = registry(w)) { r->items.append(this); r->watch(); w->update(); }
    });
    if (auto *r = registry(window())) { r->items.append(this); r->watch(); window()->update(); }
}
LiquidGlass::~LiquidGlass() { detach(this); }
void LiquidGlass::setSourceItem(QQuickItem *item) { m_hasSource = item != nullptr; m_source = item; emit materialChanged(); }

QVector<GlassShape> LiquidGlass::collect(QQuickWindow *w, const QList<LiquidGlass*> &items, bool *overflow) {
    if (overflow) *overflow = false;
    QVector<GlassShape> result;
    if (!w) return result;
    QSet<QQuickItem*> owners;
    for (auto *m : items) if (m->isEnabled()) owners.insert(m->sourceItem());
    // Scene-tree paint order (stable sibling z sort), not registration/Loader order.
    QHash<QQuickItem*, int> order;
    int serial = 0;
    std::function<void(QQuickItem*)> visit = [&](QQuickItem *i) {
        auto children = i->childItems();
        std::stable_sort(children.begin(), children.end(), [](auto *a, auto *b) { return a->z() < b->z(); });
        for (auto *c : children) if (c->z() < 0) visit(c);
        order[i] = serial++;
        for (auto *c : children) if (c->z() >= 0) visit(c);
    };
    visit(w->contentItem());
    auto sorted = items;
    std::stable_sort(sorted.begin(), sorted.end(), [&](auto *a, auto *b) { return order.value(a->sourceItem()) < order.value(b->sourceItem()); });
    for (auto *m : sorted) {
        auto *i = m->sourceItem();
        if (!i || !m->isEnabled() || !m->isVisible() || i->window() != w || i->width() <= 0 || i->height() <= 0) continue;
        GlassShape s;
        s.box = i->mapRectToItem(w->contentItem(), QRectF(0, 0, i->width(), i->height()));
        s.clip = QRectF(0, 0, w->width(), w->height());
        s.opacity = 1; s.depth = 0;
        for (auto *a = i; a; a = a->parentItem()) {
            if (!a->isVisible()) { s.opacity = 0; break; }
            s.opacity *= a->opacity();
            if (a != i && owners.contains(a)) ++s.depth;
            // Installed Quickshell Widgets/ClippingRectangle.qml is a QML Item
            // with contentInsideBorder + contentUnderBorder + contentItem, not a
            // C++ ClippingRectangle and does NOT set clip:true.
            const bool qsClip = a->metaObject()->indexOfProperty("contentInsideBorder") >= 0
                && a->metaObject()->indexOfProperty("contentUnderBorder") >= 0;
            if (a->clip() || qsClip) {
                QRectF clip(0, 0, a->width(), a->height());
                if (qsClip && a != i && a->property("contentInsideBorder").toBool()) {
                    auto *content = qvariant_cast<QQuickItem*>(a->property("contentItem"));
                    if (content) clip = content->mapRectToItem(a, QRectF(0, 0, content->width(), content->height()));
                }
                s.clip = s.clip.intersected(a->mapRectToItem(w->contentItem(), clip));
            }
        }
        auto finiteRect = [](const QRectF &r) {
            return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) && std::isfinite(r.height());
        };
        if (!std::isfinite(s.opacity) || s.opacity <= 0 || !finiteRect(s.box) || !finiteRect(s.clip)
            || s.clip.isEmpty() || !s.clip.intersects(s.box)) continue;
        if (result.size() == 128) {
            if (overflow) *overflow = true;
            break;
        }
        s.opacity = std::clamp(s.opacity, 0., 1.);
        // Protocol is axis-aligned. mapRect handles translated/scaled/reflected
        // items; arbitrary rotations/shears are represented by their bounding box.
        const auto origin = i->mapToItem(w->contentItem(), QPointF(0, 0));
        const auto dx = i->mapToItem(w->contentItem(), QPointF(i->width(), 0)) - origin;
        const auto dy = i->mapToItem(w->contentItem(), QPointF(0, i->height())) - origin;
        const qreal scale = std::min(std::hypot(dx.x(), dx.y()) / i->width(), std::hypot(dy.x(), dy.y()) / i->height());
        s.radii = {m->topLeftRadius()*scale, m->topRightRadius()*scale, m->bottomRightRadius()*scale, m->bottomLeftRadius()*scale};
        for (auto &r : s.radii) r = std::isfinite(r) ? std::clamp(r, 0., 1000000.) : 0.;
        const std::array<QPointF, 4> points{origin, origin + dx, origin + dx + dy, origin + dy};
        const auto originalRadii = s.radii;
        // Preserve independent corners across reflected axes and quarter turns.
        for (int corner = 0; corner < 4; ++corner) {
            const auto p = points[corner];
            bool left = qAbs(p.x() - s.box.left()) < 0.001;
            bool right = qAbs(p.x() - s.box.right()) < 0.001;
            bool top = qAbs(p.y() - s.box.top()) < 0.001;
            bool bottom = qAbs(p.y() - s.box.bottom()) < 0.001;
            if ((left || right) && (top || bottom)) s.radii[top ? (left ? 0 : 1) : (right ? 2 : 3)] = originalRadii[corner];
        }
        const QColor c = m->m_tint;
        s.tint = (quint32(c.red()) << 24) | (quint32(c.green()) << 16) | (quint32(c.blue()) << 8) | quint32(c.alpha());
        s.preset = m->m_preset;
        result.append(s);
    }
    return result;
}
GlassManager::GlassManager(QObject *p) : QObject(p) {
    connect(&transport(), &Transport::availabilityChanged, this, &GlassManager::availabilityChanged);
}
bool GlassManager::active() const { return transport().active(); }
bool GlassManager::markerReady() const { return transport().ready; }
bool GlassManager::available() const { return transport().available(); }
#include "liquidglass.moc"
