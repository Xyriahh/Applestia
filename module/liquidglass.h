#pragma once
#include <QQuickItem>
#include <QColor>
#include <QPointer>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <array>

struct GlassShape {
    QRectF box, clip;
    std::array<qreal, 4> radii;
    qreal opacity;
    quint32 depth, tint;
    QString preset;
};

class LiquidGlass : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal radius MEMBER m_radius NOTIFY materialChanged)
    Q_PROPERTY(qreal topLeftRadius READ topLeftRadius WRITE setTopLeftRadius NOTIFY materialChanged)
    Q_PROPERTY(qreal topRightRadius READ topRightRadius WRITE setTopRightRadius NOTIFY materialChanged)
    Q_PROPERTY(qreal bottomRightRadius READ bottomRightRadius WRITE setBottomRightRadius NOTIFY materialChanged)
    Q_PROPERTY(qreal bottomLeftRadius READ bottomLeftRadius WRITE setBottomLeftRadius NOTIFY materialChanged)
    Q_PROPERTY(QColor tint MEMBER m_tint NOTIFY materialChanged)
    Q_PROPERTY(QString preset MEMBER m_preset NOTIFY materialChanged)
    Q_PROPERTY(QQuickItem* sourceItem READ sourceItem WRITE setSourceItem NOTIFY materialChanged)
public:
    explicit LiquidGlass(QQuickItem *parent = nullptr);
    ~LiquidGlass() override;
    qreal topLeftRadius() const { return corner(0); }
    qreal topRightRadius() const { return corner(1); }
    qreal bottomRightRadius() const { return corner(2); }
    qreal bottomLeftRadius() const { return corner(3); }
    void setTopLeftRadius(qreal v) { setCorner(0, v); }
    void setTopRightRadius(qreal v) { setCorner(1, v); }
    void setBottomRightRadius(qreal v) { setCorner(2, v); }
    void setBottomLeftRadius(qreal v) { setCorner(3, v); }
    QQuickItem *sourceItem() const { return m_hasSource ? m_source.data() : const_cast<LiquidGlass*>(this); }
    void setSourceItem(QQuickItem *item);
    static QVector<GlassShape> collect(QQuickWindow *window, const QList<LiquidGlass*> &items, bool *overflow = nullptr);
signals:
    void materialChanged();
private:
    qreal corner(int i) const { return m_corners[i] < 0 ? m_radius : m_corners[i]; }
    void setCorner(int i, qreal v) { m_corners[i] = v; emit materialChanged(); }
    qreal m_radius = 0;
    std::array<qreal, 4> m_corners{-1, -1, -1, -1};
    QColor m_tint = QColor(16, 16, 20, 66);
    QString m_preset = QStringLiteral("applestia_control");
    QPointer<QQuickItem> m_source;
    bool m_hasSource = false;
};

class GlassManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool active READ active NOTIFY availabilityChanged)
    Q_PROPERTY(bool markerReady READ markerReady NOTIFY availabilityChanged)
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)
public:
    explicit GlassManager(QObject *parent = nullptr);
    bool active() const;
    bool markerReady() const;
    // Visible-material overflow latches local fallback until shell restart;
    // disappearing fallback Loaders must not automatically re-enable native UI.
    bool available() const;
signals:
    void availabilityChanged();
};
