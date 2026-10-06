#include "liquidglass.h"
#include "client-test-hooks.h"
#include "wayland-applestia-glass-shapes-v1-client-protocol.h"
#include <QQuickWindow>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QtTest>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

namespace GlassClientTest {
// Test-only GUI capability injection; no compositor, global withdrawal or
// production reset path. The overflow latch deliberately survives all of these.
void setCapability(bool active, bool markerReady);
void prepareFrame(QQuickWindow *window);
}

namespace {
QList<QQuickItem*> visualItems(QQuickItem *root) {
    QList<QQuickItem*> result;
    for (auto *child : root->childItems()) {
        result.append(child);
        result.append(visualItems(child));
    }
    return result;
}
// Decode actual marshalled shape requests. Commit is modelled separately to
// assert the protocol's pending/committed boundary without a compositor server.
struct ShapePeer {
    int pending = 0, committed = 0, destroys = 0, begins = 0;
    struct QueuedFrame { int shapes; bool fallbackForeground; };
    QList<QueuedFrame> queued;
    bool committedFallbackForeground = false;
    bool drain(int fd) {
        QByteArray wire;
        char buffer[8192];
        for (ssize_t n; (n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0; ) wire.append(buffer, n);
        qsizetype offset = 0;
        while (offset < wire.size()) {
            if (offset + 8 > wire.size()) return false;
            quint32 header;
            std::memcpy(&header, wire.constData() + offset + 4, sizeof(header));
            const auto size = header >> 16;
            if (size < 8 || offset + size > wire.size()) return false;
            switch (header & 0xffff) {
            case APPLESTIA_GLASS_SHAPES_V1_DESTROY: ++destroys; pending = committed = 0; break;
            case APPLESTIA_GLASS_SHAPES_V1_BEGIN: ++begins; pending = 0; break;
            case APPLESTIA_GLASS_SHAPES_V1_ADD_SHAPE: ++pending; break;
            case APPLESTIA_GLASS_SHAPES_V1_CLEAR: pending = 0; break;
            }
            offset += size;
        }
        return true;
    }
    void commit() { committed = pending; }
    void submit(bool fallbackForeground) { queued.append({pending, fallbackForeground}); }
    bool apply() {
        if (queued.isEmpty()) return false;
        const auto frame = queued.takeFirst();
        committed = frame.shapes;
        committedFallbackForeground = frame.fallbackForeground;
        return true;
    }
};
}
class Tests : public QObject {
    Q_OBJECT
private slots:
    void removalWaitsForFrameCommit() {
        int sockets[2];
        QVERIFY(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        auto *display = wl_display_connect_to_fd(sockets[0]); QVERIFY(display);
        ShapePeer peer;
        {
            QQuickWindow w; w.resize(300, 200);
            auto *first = new LiquidGlass(w.contentItem()); first->setSize({80, 60});
            auto *remaining = new LiquidGlass(w.contentItem()); remaining->setSize({50, 40}); remaining->setX(100);
            auto *proxy = reinterpret_cast<applestia_glass_shapes_v1*>(wl_proxy_create(reinterpret_cast<wl_proxy*>(display), &applestia_glass_shapes_v1_interface));
            QVERIFY(proxy);
            GlassClientTest::attachAssociation(&w, proxy);
            QVERIFY(GlassClientTest::publishFrame(&w));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.pending, 2); peer.commit(); QCOMPARE(peer.committed, 2);

            delete first;
            QCOMPARE(GlassClientTest::association(&w), proxy);
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.destroys, 0); QCOMPARE(peer.committed, 2);

            QVERIFY(GlassClientTest::publishFrame(&w));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.pending, 1); QCOMPARE(peer.committed, 2); QCOMPARE(peer.destroys, 0);
            peer.commit(); QCOMPARE(peer.committed, 1);

            delete remaining;
            QCOMPARE(GlassClientTest::association(&w), proxy);
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.committed, 1); QCOMPARE(peer.destroys, 0);
            QVERIFY(GlassClientTest::publishFrame(&w));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.begins, 3); QCOMPARE(peer.pending, 0); QCOMPARE(peer.committed, 1);
            QCOMPARE(peer.destroys, 0); peer.commit(); QCOMPARE(peer.committed, 0);
        }
        // Window destruction, unlike ordinary material removal, releases hints.
        QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
        QCOMPARE(peer.destroys, 1);
        wl_display_disconnect(display);
        close(sockets[1]);
    }
    void nonRenderingGeometrySchedulesFrame() {
        QQuickWindow w; w.resize(300, 200);
        QQuickItem owner(w.contentItem()); owner.setSize({80, 60});
        LiquidGlass marker(&owner); marker.setSourceItem(&owner);
        QSignalSpy frames(&w, &QQuickWindow::frameSwapped);
        w.show();
        QTRY_VERIFY(frames.count() > 0);
        QTest::qWait(30); frames.clear();
        owner.setX(25);
        QTRY_VERIFY(frames.count() > 0);
    }
    void ownersAndClip() {
        QQuickWindow w; w.resize(300, 200);
        QQuickItem card(w.contentItem()); card.setSize({100, 100}); card.setPosition({20, 10}); card.setOpacity(.5);
        LiquidGlass parent(&card); parent.setSourceItem(&card);
        QQuickItem child(&card); child.setSize({60, 50}); child.setX(80);
        LiquidGlass marker(&child); marker.setSourceItem(&child); marker.setTopLeftRadius(15);
        card.setClip(true);
        auto shapes = LiquidGlass::collect(&w, {&marker, &parent});
        QCOMPARE(shapes.size(), 2);
        QCOMPARE(shapes[0].depth, 0u); QCOMPARE(shapes[1].depth, 1u);
        QCOMPARE(shapes[1].box.width(), 60.); QCOMPARE(shapes[1].clip.width(), 100.);
        QCOMPARE(shapes[1].radii[0], 15.); QCOMPARE(shapes[1].opacity, .5);
        child.setVisible(false); QCOMPARE(LiquidGlass::collect(&w, {&marker, &parent}).size(), 1);
    }
    void nestingAndCap() {
        QQuickWindow w; w.resize(100, 100);
        LiquidGlass parent(w.contentItem()); parent.setSize({80, 80});
        LiquidGlass child(&parent); child.setSize({50, 50});
        QCOMPARE(LiquidGlass::collect(&w, {&child, &parent})[1].depth, 1u);
        QCOMPARE(LiquidGlass::collect(&w, {&child, &parent})[1].preset, QString("applestia_control"));
        QVERIFY(parent.setProperty("preset", "custom_control"));
        QCOMPARE(LiquidGlass::collect(&w, {&child, &parent})[0].preset, QString("custom_control"));
        QList<LiquidGlass*> list;
        for (int i=0; i<140; ++i) { auto *m = new LiquidGlass(w.contentItem()); m->setSize({10,10}); list.append(m); }
        QCOMPARE(LiquidGlass::collect(&w, list).size(), 128);
        qDeleteAll(list);
    }
    void quarterTurnAndSourceDestruction() {
        QQuickWindow w; w.resize(300, 200);
        auto *owner = new QQuickItem(w.contentItem()); owner->setSize({100, 60}); owner->setPosition({100, 50});
        owner->setRotation(90);
        LiquidGlass marker(w.contentItem()); marker.setSourceItem(owner); marker.setTopLeftRadius(10); marker.setTopRightRadius(20);
        auto shapes = LiquidGlass::collect(&w, {&marker});
        QCOMPARE(shapes.size(), 1);
        QCOMPARE(shapes[0].box.width(), 60.);
        QCOMPARE(shapes[0].radii[1], 10.);
        QCOMPARE(shapes[0].radii[2], 20.);
        delete owner;
        QVERIFY(LiquidGlass::collect(&w, {&marker}).isEmpty());
    }
    void quickshellClipMetadata() {
        QQuickWindow w; w.resize(200, 200);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nItem { id: root; width: 100; height: 100;"
            "property bool contentInsideBorder: true; property bool contentUnderBorder: false;"
            "property Item contentItem: Item { parent: root; x: 3; y: 3; width: 94; height: 94;"
            "Item { objectName: 'source'; x: 80; width: 50; height: 50 } } }", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *clip = qobject_cast<QQuickItem*>(object.data()); clip->setParentItem(w.contentItem());
        auto *source = clip->findChild<QQuickItem*>("source"); QVERIFY(source);
        LiquidGlass marker(w.contentItem()); marker.setSourceItem(source); marker.setTopLeftRadius(15);
        auto shapes = LiquidGlass::collect(&w, {&marker});
        QCOMPARE(shapes.size(), 1);
        QCOMPARE(shapes[0].box.width(), 50.); QCOMPARE(shapes[0].box.x(), 83.);
        QCOMPARE(shapes[0].clip, QRectF(3, 3, 94, 94)); QCOMPARE(shapes[0].radii[0], 15.);
        QVERIFY(!clip->clip());
    }
    void hiddenOrClippedMaterialsDoNotOverflow() {
        GlassClientTest::setCapability(true, true);
        GlassManager manager;
        QSignalSpy availability(&manager, &GlassManager::availabilityChanged);
        QQuickWindow w; w.resize(300, 200);
        QList<LiquidGlass*> materials;
        for (int n = 0; n < 128; ++n) {
            auto *m = new LiquidGlass(w.contentItem()); m->setSize({10, 10}); materials.append(m);
        }
        QQuickItem clip(w.contentItem()); clip.setSize({50, 50}); clip.setClip(true);
        LiquidGlass extra(&clip); extra.setSize({10, 10}); materials.append(&extra);
        auto check = [&] {
            bool overflow = true;
            QCOMPARE(LiquidGlass::collect(&w, materials, &overflow).size(), 128);
            QVERIFY(!overflow);
            GlassClientTest::prepareFrame(&w); // real registry, 129 registered markers
            QVERIFY(manager.active()); QVERIFY(manager.markerReady()); QVERIFY(manager.available());
            QCOMPARE(availability.count(), 0);
        };
        extra.setVisible(false); check();
        extra.setVisible(true); extra.setX(60); check(); // outside clipping ancestor
        extra.setX(0); extra.setOpacity(0); check();
        extra.setOpacity(1); clip.setVisible(false); check();
        clip.setVisible(true); extra.setX(400); clip.setClip(false); check(); // outside window
        materials.removeLast(); qDeleteAll(materials);
        GlassClientTest::setCapability(false, false);
    }
    void overflowWithoutAvailableManagerDoesNotLatch() {
        GlassManager manager;
        QQuickWindow w; w.resize(300, 200);
        QList<LiquidGlass*> materials;
        for (int n = 0; n < 129; ++n) {
            auto *m = new LiquidGlass(w.contentItem()); m->setSize({10, 10}); materials.append(m);
        }
        bool overflow = false;
        QCOMPARE(LiquidGlass::collect(&w, materials, &overflow).size(), 128); QVERIFY(overflow);
        GlassClientTest::setCapability(false, true); // old/no protocol, even a stale marker
        GlassClientTest::prepareFrame(&w);
        QVERIFY(!manager.active()); QVERIFY(manager.markerReady()); QVERIFY(!manager.available());
        GlassClientTest::setCapability(true, false); // protocol but no readiness marker
        GlassClientTest::prepareFrame(&w);
        QVERIFY(manager.active()); QVERIFY(!manager.markerReady()); QVERIFY(!manager.available());
        materials.last()->setVisible(false);
        GlassClientTest::setCapability(true, true);
        GlassClientTest::prepareFrame(&w);
        QVERIFY(manager.available()); // neither unavailable state latched overflow
        qDeleteAll(materials);
        GlassClientTest::setCapability(false, false);
    }
    // Keep last: overflow is fatal for this process, exactly as in the shell.
    void registeredOverflowRestoresFallbackWithoutRetryLoop() {
        int sockets[2];
        QVERIFY(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        auto *display = wl_display_connect_to_fd(sockets[0]); QVERIFY(display);
        ShapePeer peer;
        {
            GlassClientTest::setCapability(true, true);
            GlassManager manager;
            QSignalSpy availability(&manager, &GlassManager::availabilityChanged);
            QQuickWindow w; w.resize(300, 200);
            QQmlEngine engine;
            engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
            QQmlComponent component(&engine);
            component.setData(R"QML(
                import QtQuick
                import Applestia.Glass
                Item {
                    id: root
                    width: 300; height: 200
                    property int materialCount: 128
                    readonly property bool nativeAvailable: GlassManager.available
                    Repeater {
                        model: root.materialCount
                        delegate: Item {
                            width: 10; height: 10
                            Loader {
                                id: native
                                anchors.fill: parent
                                active: GlassManager.available
                                sourceComponent: LiquidGlass { width: 10; height: 10 }
                            }
                            Rectangle {
                                objectName: "fallback"
                                anchors.fill: parent
                                visible: !native.active
                                color: "red"
                            }
                        }
                    }
                }
            )QML", QUrl());
            QScopedPointer<QObject> root(component.create());
            QVERIFY2(root, qPrintable(component.errorString()));
            auto *item = qobject_cast<QQuickItem*>(root.data()); QVERIFY(item);
            item->setParentItem(w.contentItem());
            QCoreApplication::processEvents();
            QVERIFY(root->property("nativeAvailable").toBool());
            // Repeater delegates need not have QObject parents under root; use
            // the same visual tree that determines the registry's window/order.
            auto markerCount = [&] {
                int count = 0;
                for (auto *child : visualItems(item)) if (qobject_cast<LiquidGlass*>(child)) ++count;
                return count;
            };
            QCOMPARE(markerCount(), 128);
            auto *proxy = reinterpret_cast<applestia_glass_shapes_v1*>(wl_proxy_create(reinterpret_cast<wl_proxy*>(display), &applestia_glass_shapes_v1_interface));
            QVERIFY(proxy);
            GlassClientTest::attachAssociation(&w, proxy);
            QVERIFY(GlassClientTest::publishFrame(&w));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.pending, 128); peer.commit(); QCOMPARE(peer.committed, 128);

            root->setProperty("materialCount", 129);
            QCOMPARE(markerCount(), 129);
            QTest::ignoreMessage(QtWarningMsg, "Applestia.Glass: more than 128 visible glass materials in one window; native glass disabled locally until shell restart (using legacy fallback).");
            GlassClientTest::prepareFrame(&w); // normal registry collection detects overflow
            QCoreApplication::processEvents();
            QVERIFY(manager.active()); QVERIFY(manager.markerReady()); QVERIFY(!manager.available());
            QCOMPARE(availability.count(), 1);
            QCOMPARE(markerCount(), 0);
            QList<QQuickItem*> fallback;
            for (auto *child : visualItems(item)) if (child->objectName() == "fallback") fallback.append(child);
            QCOMPARE(fallback.size(), 129);
            for (auto *body : fallback) {
                QVERIFY(body->isVisible());
                QCOMPARE(body->property("color").value<QColor>(), QColor("red"));
            }
            QCOMPARE(GlassClientTest::association(&w), proxy);
            // Notification must not retire an older native frame still in flight.
            QVERIFY(QMetaObject::invokeMethod(&w, "afterFrameEnd", Qt::DirectConnection));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.destroys, 0); QCOMPARE(peer.committed, 128);
            QVERIFY(GlassClientTest::publishFrame(&w));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.pending, 0); QCOMPARE(peer.committed, 128); QCOMPARE(peer.destroys, 0);
            peer.submit(true); // queued fallback commit blocked on a fence/FIFO/timer
            QVERIFY(QMetaObject::invokeMethod(&w, "afterFrameEnd", Qt::DirectConnection));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.destroys, 0); QCOMPARE(peer.committed, 128);
            QVERIFY(!peer.committedFallbackForeground); // old native foreground still displayed
            QCOMPARE(GlassClientTest::association(&w), proxy);
            // A second submitted empty frame must not destroy the association or
            // replace the hints paired with the still-applied native foreground.
            QVERIFY(GlassClientTest::publishFrame(&w));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            peer.submit(true);
            QVERIFY(QMetaObject::invokeMethod(&w, "afterFrameEnd", Qt::DirectConnection));
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.pending, 0); QCOMPARE(peer.committed, 128); QCOMPARE(peer.destroys, 0);
            QVERIFY(!peer.committedFallbackForeground);
            QVERIFY(peer.apply()); // actual application, not client submission/frame end
            QCOMPARE(peer.committed, 0); QVERIFY(peer.committedFallbackForeground);
            QCOMPARE(GlassClientTest::association(&w), proxy); QCOMPARE(peer.destroys, 0);
            QVERIFY(peer.apply()); QCOMPARE(peer.committed, 0); QVERIFY(peer.committedFallbackForeground);

            for (int count : {128, 0, 129, 128}) {
                root->setProperty("materialCount", count);
                QCoreApplication::processEvents();
                QVERIFY(GlassClientTest::publishFrame(&w));
                QVERIFY(QMetaObject::invokeMethod(&w, "afterFrameEnd", Qt::DirectConnection));
                QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
                QVERIFY(!manager.available());
                QCOMPARE(markerCount(), 0);
                QCOMPARE(availability.count(), 1); // no reactivation/Loader retry loop
                QCOMPARE(peer.pending, 0); QCOMPARE(peer.committed, 0); QCOMPARE(peer.destroys, 0);
                QCOMPARE(GlassClientTest::association(&w), proxy);
            }
            QCOMPARE(peer.begins, 7);
            GlassClientTest::setCapability(false, true);
            QVERIFY(!manager.active()); QVERIFY(manager.markerReady()); QVERIFY(!manager.available());
            QVERIFY(wl_display_flush(display) >= 0); QVERIFY(peer.drain(sockets[1]));
            QCOMPARE(peer.destroys, 1); // real global loss, not local overflow/frame end
            QVERIFY(!GlassClientTest::association(&w));
            GlassClientTest::setCapability(true, true); // new generation must not reset latch
            QVERIFY(manager.active()); QVERIFY(manager.markerReady()); QVERIFY(!manager.available());
            QQuickWindow replacement; replacement.resize(100, 100);
            LiquidGlass lone(replacement.contentItem()); lone.setSize({10, 10});
            GlassClientTest::prepareFrame(&replacement);
            QVERIFY(!manager.available()); // new/empty windows cannot reset it either
        }
        wl_display_disconnect(display);
        close(sockets[1]);
    }
};
QTEST_MAIN(Tests)
#include "tests.moc"
