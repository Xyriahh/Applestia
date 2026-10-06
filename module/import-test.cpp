#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QDebug>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    engine.addImportPath(QString::fromLocal8Bit(argv[1]));
    QQmlComponent c(&engine);
    c.setData("import QtQuick\nimport Applestia.Glass\nLiquidGlass { radius: 12; tint: '#336699aa'; property bool supported: GlassManager.available }", QUrl());
    auto *object = c.create();
    if (!object) { qWarning() << c.errors(); return 1; }
    if (object->property("supported").toBool()) return 2;
    delete object;
    return 0;
}
