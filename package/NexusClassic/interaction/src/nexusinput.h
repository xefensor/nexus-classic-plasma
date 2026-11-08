// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSharedPointer>
#include <QVector2D>

class NexusCanvas;

class NexusRuntime : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void activate(const QUrl &bridgeUrl);
};

class NexusState : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *target READ target WRITE setTarget NOTIFY settingsChanged)
    Q_PROPERTY(bool mouseEnabled READ mouseEnabled WRITE setMouseEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool shared READ shared WRITE setShared NOTIFY settingsChanged)
    Q_PROPERTY(QVector2D desktopPixels READ desktopPixels WRITE setDesktopPixels NOTIFY settingsChanged)
    Q_PROPERTY(QVector2D screenOffset READ screenOffset WRITE setScreenOffset NOTIFY settingsChanged)
    Q_PROPERTY(qreal pixelRatio READ pixelRatio WRITE setPixelRatio NOTIFY settingsChanged)
    Q_PROPERTY(QUrl textureUrl READ textureUrl NOTIFY textureChanged)
    Q_PROPERTY(int extraCount READ extraCount NOTIFY textureChanged)
public:
    explicit NexusState(QObject *parent = nullptr);
    ~NexusState() override;
    QQuickItem *target() const { return m_target; }
    void setTarget(QQuickItem *target);
    bool mouseEnabled() const { return m_mouseEnabled; }
    void setMouseEnabled(bool value);
    bool shared() const { return m_shared; }
    void setShared(bool value);
    QVector2D desktopPixels() const { return m_desktopPixels; }
    void setDesktopPixels(QVector2D value);
    QVector2D screenOffset() const { return m_screenOffset; }
    void setScreenOffset(QVector2D value);
    qreal pixelRatio() const { return m_pixelRatio; }
    void setPixelRatio(qreal value);
    QUrl textureUrl() const;
    int extraCount() const;
Q_SIGNALS:
    void settingsChanged();
    void textureChanged();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void scheduleUpdate();
    void updateCanvas();
    void refreshTargetShader();
    QPointer<QQuickItem> m_target;
    QPointer<QQuickWindow> m_window;
    QSharedPointer<NexusCanvas> m_canvas;
    bool m_mouseEnabled = true;
    bool m_shared = false;
    bool m_updatePending = false;
    bool m_pressed = false;
    bool m_dragged = false;
    QPointF m_pressPosition;
    QVector2D m_desktopPixels;
    QVector2D m_screenOffset;
    qreal m_pixelRatio = 1;
};
