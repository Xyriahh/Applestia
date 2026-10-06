#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtTest>

// Exercise the real staged wrappers with minimal colour/animation services,
// without launching Quickshell, connecting to a compositor, or loading desktop
// services. Use installed ClippingRectangle QML, substituting only its static
// Quickshell C++ border value type; this test does not validate GPU clip shaders.
class ShellTests : public QObject {
    Q_OBJECT
private slots:
    void wrappers();
    void testHooks();
    void productionHookCleanup();
};
void ShellTests::wrappers() {
        QTemporaryDir temp("/tmp/opencode/glass-shell-XXXXXX");
        QVERIFY(temp.isValid());
        const QString dir = temp.path();
        QDir().mkpath(dir + "/qs/components/glass");
        QDir().mkpath(dir + "/glass");
        QDir().mkpath(dir + "/Quickshell/Widgets");
        auto write = [](const QString &path, const QByteArray &text) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
        };
        QVERIFY(write(dir + "/CAnim.qml", "import QtQuick\nColorAnimation { duration: 0 }"));
        QFile installed("/usr/lib/qt6/qml/Quickshell/Widgets/ClippingRectangle.qml");
        QVERIFY(installed.open(QIODevice::ReadOnly));
        auto clipSource = installed.readAll();
        clipSource.replace("property clippingRectangleBorder border", "component Border: QtObject { property color color: 'transparent'; property real width: 0; property bool pixelAligned: true }\nproperty Border border: Border {}");
        QVERIFY(write(dir + "/Quickshell/Widgets/ClippingRectangle.qml", clipSource));
        QVERIFY(write(dir + "/Quickshell/Widgets/qmldir", "module Quickshell.Widgets\nClippingRectangle 1.0 ClippingRectangle.qml\n"));
        QVERIFY(write(dir + "/qs/components/glass/qmldir", "module qs.components.glass\nsingleton InnerGlass 1.0 InnerGlass.qml\nGlassFill 1.0 GlassFill.qml\n"));
        QVERIFY(write(dir + "/qs/components/glass/InnerGlass.qml", "pragma Singleton\nimport QtQuick\nQtObject { property bool mode: false; readonly property color innerGlassTint: '#44101014'; function nativeEligible(item) { return mode; } function registerSurface(item) {} function unregisterSurface(item) {} }"));
        QVERIFY(write(dir + "/qs/components/glass/GlassFill.qml", "import QtQuick\nRectangle { property real frost; color: '#44101014' }"));
        const QString shell = QStringLiteral(SHELL_SOURCE);
        QVERIFY(QFile::copy(shell + "/components/StyledRect.qml", dir + "/StyledRect.qml"));
        QVERIFY(QFile::copy(shell + "/components/StyledClippingRect.qml", dir + "/StyledClippingRect.qml"));
        QVERIFY(QFile::copy(shell + "/components/glass/NativeMaterial.qml", dir + "/glass/NativeMaterial.qml"));
        QQmlEngine engine;
        engine.addImportPath(dir);
        engine.addImportPath(QStringLiteral(QML_BUILD));
        for (const QByteArray type : {QByteArray("StyledRect"), QByteArray("StyledClippingRect")}) {
            QQmlComponent c(&engine);
            c.setData("import QtQuick\nimport qs.components.glass\n" + type + R"QML( {
                width: 100; height: 80; radius: 12; topLeftRadius: 4
                glass: true
                property bool selected: false
                property bool testNative: false
                onTestNativeChanged: InnerGlass.mode = testNative
                color: selected ? "#ff336699" : "transparent"
                border.width: 1; border.color: "white"
                Item { objectName: "foreground"; width: 15; height: 10 }
            })QML", QUrl::fromLocalFile(dir + "/test.qml"));
            QScopedPointer<QObject> root(c.create());
            QVERIFY2(root, qPrintable(c.errorString()));
            QVERIFY(root->findChild<QQuickItem*>("foreground"));
            QVERIFY(!root->property("nativeGlass").toBool());
            root->setProperty("testNative", true);
            QCoreApplication::processEvents();
            QVERIFY(root->property("nativeGlass").toBool());
            QVERIFY(root->setProperty("selected", true));
            QCOMPARE(root->property("color").value<QColor>(), QColor("#ff336699"));
            auto markers = root->findChildren<QObject*>();
            bool found = false;
            for (auto *child : markers) if (child->metaObject()->indexOfProperty("sourceItem") >= 0 && child->metaObject()->indexOfProperty("preset") >= 0) {
                found = true;
                QCOMPARE(child->property("sourceItem").value<QQuickItem*>(), qobject_cast<QQuickItem*>(root.data()));
                QCOMPARE(child->property("topLeftRadius").toDouble(), 4.);
                QCOMPARE(child->property("preset").toString(), QString("applestia_control"));
                QVERIFY(root->setProperty("glassPreset", "custom_control"));
                QCOMPARE(child->property("preset").toString(), QString("custom_control"));
                QVERIFY(root->setProperty("glassPreset", "applestia_control"));
            }
            QVERIFY(found);
            root->setProperty("enabled", false);
            QVERIFY(!root->property("nativeGlass").toBool());
            root->setProperty("enabled", true);
            QVERIFY(root->property("nativeGlass").toBool());
            root->setProperty("testNative", false);
            QCoreApplication::processEvents();
            QVERIFY(!root->property("nativeGlass").toBool());
            QCOMPARE(root->property("color").value<QColor>(), QColor("#ff336699"));
            root->setProperty("selected", false);
            QCOMPARE(root->property("color").value<QColor>(), QColor("transparent"));
        }
        // Regression: capability is already available BEFORE card/Loader
        // construction. Loader's default active:true emits no activeChanged.
        auto *inner = engine.singletonInstance<QObject*>("qs.components.glass", "InnerGlass");
        QVERIFY(inner); QVERIFY(inner->setProperty("mode", true));
        for (const QByteArray type : {QByteArray("StyledRect"), QByteArray("StyledClippingRect")}) {
            QQmlComponent c(&engine);
            c.setData("import QtQuick\n" + type + " { width: 100; height: 80; radius: 12; glass: true }", QUrl::fromLocalFile(dir + "/initial.qml"));
            QScopedPointer<QObject> root(c.create());
            QVERIFY2(root, qPrintable(c.errorString()));
            QVERIFY(root->property("nativeGlass").toBool());
            int markers = 0;
            for (auto *child : root->findChildren<QObject*>()) {
                if (child->metaObject()->indexOfProperty("sourceItem") < 0 || child->metaObject()->indexOfProperty("preset") < 0) continue;
                ++markers;
                QCOMPARE(child->property("sourceItem").value<QQuickItem*>(), qobject_cast<QQuickItem*>(root.data()));
                QCOMPARE(child->property("preset").toString(), QString("applestia_control"));
            }
            QCOMPARE(markers, 1);
        }
}
void ShellTests::testHooks() {
    QTemporaryDir temp("/tmp/opencode/glass-hooks-XXXXXX");
    QVERIFY(temp.isValid());
    const QString dir = temp.path();
    auto write = [](const QString &path, const QByteArray &text) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
    };
    for (const auto &module : {"Quickshell/Io", "qs/services", "qs/components/glass"}) QDir().mkpath(dir + "/" + module);
    QVERIFY(write(dir + "/Quickshell/qmldir", "module Quickshell\nsingleton Quickshell 1.0 Quickshell.qml\nScope 1.0 Scope.qml\n"));
    QVERIFY(write(dir + "/Quickshell/Quickshell.qml", "pragma Singleton\nimport QtQuick\nQtObject { property string flag: '1'; readonly property string shellDir: '/staged/shell'; function env(name) { return name === 'APPLESTIA_V2_TEST_HOOKS' ? flag : ''; } }"));
    QVERIFY(write(dir + "/Quickshell/Scope.qml", "import QtQuick\nItem {}"));
    QVERIFY(write(dir + "/Quickshell/Io/qmldir", "module Quickshell.Io\nIpcHandler 1.0 IpcHandler.qml\n"));
    QVERIFY(write(dir + "/Quickshell/Io/IpcHandler.qml", "import QtQuick\nQtObject { property string target }"));
    QVERIFY(write(dir + "/qs/services/qmldir", "module qs.services\nsingleton ShellState 1.0 ShellState.qml\n"));
    QVERIFY(write(dir + "/qs/services/ShellState.qml", "pragma Singleton\nimport QtQuick\nQtObject { property int dashboardTab: 0; property bool dashboard: false; property bool utilities: false; property bool sidebar: false; property bool launcher: false; property bool osd: false; property bool session: false; property bool expanded: false; function forActive() { return this; } function componentsForActive() { return this; } function find(name) { return name === 'applestiaV2RecordSplit' ? this : null; } }"));
    QVERIFY(write(dir + "/qs/components/glass/qmldir", "module qs.components.glass\nsingleton InnerGlass 1.0 InnerGlass.qml\n"));
    QVERIFY(write(dir + "/qs/components/glass/InnerGlass.qml", "pragma Singleton\nimport QtQuick\nQtObject { property var nativeCapability: ({active: true, available: true}); property bool shapesAvailable: true; property var frames: ({fixture: []}) }"));
    QVERIFY(QFile::copy(QStringLiteral(SHELL_SOURCE) + "/../client/fixtures/V2TestHooksFixture.qml", dir + "/V2TestHooks.qml"));
    QQmlEngine engine; engine.addImportPath(dir);
    QQmlComponent c(&engine, QUrl::fromLocalFile(dir + "/V2TestHooks.qml"));
    QScopedPointer<QObject> helper(c.create()); QVERIFY2(helper, qPrintable(c.errorString()));
    auto *ipc = helper->findChild<QObject*>("applestiaV2TestIpc"); QVERIFY(ipc);
    QCOMPARE(ipc->property("target").toString(), QString("v2Test"));
    auto *state = engine.singletonInstance<QObject*>("qs.services", "ShellState"); QVERIFY(state);
    QVERIFY(QMetaObject::invokeMethod(ipc, "tab", Q_ARG(int, 99))); QCOMPARE(state->property("dashboardTab").toInt(), 4);
    QVERIFY(QMetaObject::invokeMethod(ipc, "tab", Q_ARG(int, -1))); QCOMPARE(state->property("dashboardTab").toInt(), 0);
    for (const QString drawer : {"dashboard", "utilities", "sidebar", "launcher", "osd"}) {
        QVERIFY(QMetaObject::invokeMethod(ipc, "show", Q_ARG(QString, drawer), Q_ARG(bool, true))); QVERIFY(state->property(drawer.toUtf8()).toBool());
        QVERIFY(QMetaObject::invokeMethod(ipc, "show", Q_ARG(QString, drawer), Q_ARG(bool, false))); QVERIFY(!state->property(drawer.toUtf8()).toBool());
    }
    QVERIFY(QMetaObject::invokeMethod(ipc, "show", Q_ARG(QString, "session"), Q_ARG(bool, true))); QVERIFY(!state->property("session").toBool());
    QString result;
    QVERIFY(QMetaObject::invokeMethod(ipc, "recordMenu", Q_RETURN_ARG(QString, result), Q_ARG(bool, true))); QCOMPARE(result, QString("1")); QVERIFY(state->property("expanded").toBool());
    QVERIFY(QMetaObject::invokeMethod(ipc, "recordMenu", Q_RETURN_ARG(QString, result), Q_ARG(bool, false))); QCOMPARE(result, QString("0"));
    QVERIFY(QMetaObject::invokeMethod(ipc, "capability", Q_RETURN_ARG(QString, result)));
    auto capability = QJsonDocument::fromJson(result.toUtf8()).object(); QVERIFY(capability["active"].toBool()); QVERIFY(capability["available"].toBool()); QVERIFY(capability["shapesAvailable"].toBool()); QCOMPARE(capability["overlayFrameScreens"].toArray()[0].toString(), QString("fixture"));
    QVERIFY(QMetaObject::invokeMethod(ipc, "stats", Q_RETURN_ARG(QString, result))); QCOMPARE(QJsonDocument::fromJson(result.toUtf8()).object()["configPath"].toString(), QString("/staged/shell"));
    auto *qs = engine.singletonInstance<QObject*>("Quickshell", "Quickshell"); QVERIFY(qs); qs->setProperty("flag", "0");
    QCOMPARE(ipc->property("target").toString(), QString());
    QVERIFY(QMetaObject::invokeMethod(ipc, "tab", Q_ARG(int, 3))); QCOMPARE(state->property("dashboardTab").toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(ipc, "show", Q_ARG(QString, "dashboard"), Q_ARG(bool, true))); QVERIFY(!state->property("dashboard").toBool());
    QVERIFY(QMetaObject::invokeMethod(ipc, "recordMenu", Q_RETURN_ARG(QString, result), Q_ARG(bool, true))); QCOMPARE(result, QString("disabled")); QVERIFY(!state->property("expanded").toBool());
    QQmlComponent gated(&engine);
    gated.setData("import QtQuick\nimport Quickshell\nLoader { active: Quickshell.env('APPLESTIA_V2_TEST_HOOKS') === '1'; source: active ? 'V2TestHooks.qml' : '' }", QUrl::fromLocalFile(dir + "/gate.qml"));
    QScopedPointer<QObject> loader(gated.create()); QVERIFY2(loader, qPrintable(gated.errorString()));
    QVERIFY(!loader->property("item").value<QObject*>());
    qs->setProperty("flag", "1");
    QTRY_VERIFY(loader->property("item").value<QObject*>());
    QVERIFY(loader->findChild<QObject*>("applestiaV2TestIpc"));
    qs->setProperty("flag", "0");
    QVERIFY(!loader->property("item").value<QObject*>());
}
void ShellTests::productionHookCleanup() {
    const QString shell = QStringLiteral(SHELL_SOURCE);
    QFile rootFile(shell + "/shell.qml"); QVERIFY(rootFile.open(QIODevice::ReadOnly));
    const auto rootSource = rootFile.readAll();
    QFile recordFile(shell + "/modules/utilities/cards/Record.qml"); QVERIFY(recordFile.open(QIODevice::ReadOnly));
    const auto recordSource = recordFile.readAll();
    const bool pendingAcceptance = QFile::exists(shell + "/modules/V2TestHooks.qml");
    if (qEnvironmentVariable("APPLESTIA_V2_REQUIRE_CLEAN_SHELL") == "1") {
        QVERIFY2(!pendingAcceptance, "Apply tests/remove-test-hooks.patch after acceptance before release verification");
    }
    if (pendingAcceptance) {
        // While the orchestrator's soak is running, do not require or mutate
        // cleanup. Still reject an inconsistent partially removed hook bundle.
        QVERIFY(rootSource.contains("modules/V2TestHooks.qml"));
        QVERIFY(rootSource.contains("APPLESTIA_V2_TEST_HOOKS"));
        QVERIFY(recordSource.contains("applestiaV2RecordSplit"));
    } else {
        for (const auto &source : {rootSource, recordSource}) {
            QVERIFY(!source.contains("V2TestHooks"));
            QVERIFY(!source.contains("APPLESTIA_V2_TEST_HOOKS"));
            QVERIFY(!source.contains("APPLESTIA-V2-TEST"));
            QVERIFY(!source.contains("applestiaV2RecordSplit"));
        }
        QVERIFY(rootSource.contains("ControlGlass {}"));
        QVERIFY(recordSource.contains("nativeTinted: false")); // keep solid Stop exception
    }
}
QTEST_MAIN(ShellTests)
#include "shell-tests.moc"
