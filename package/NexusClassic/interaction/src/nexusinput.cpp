// SPDX-License-Identifier: Apache-2.0
#include "nexusinput.h"
#include "nexusmodel.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QMouseEvent>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <utility>

class NexusCanvas : public QObject {
    Q_OBJECT
public:
    NexusCanvas(QSizeF size)
        : model(size, QDateTime::currentMSecsSinceEpoch(), QRandomGenerator::global()->generate()),
          directory(cachePath() + "/canvas-XXXXXX")
    {
        connect(&timer, &QTimer::timeout, this, [this] {
            if (model.advance(QDateTime::currentMSecsSinceEpoch())) publish();
        });
        // The original RenderScript root returned a 45 ms frame delay.
        timer.start(45);
        publish();
    }
    static QString cachePath()
    {
        QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/nexus-classic";
        QDir().mkpath(path);
        return path;
    }
    void publish()
    {
        QSaveFile file(directory.filePath("state.png"));
        if (!file.open(QIODevice::WriteOnly) || !model.image().save(&file, "PNG") || !file.commit()) {
            qWarning("Nexus Classic: cannot save pulse state");
            return;
        }
        url = QUrl::fromLocalFile(file.fileName());
        url.setQuery("revision=" + QString::number(++revision));
        Q_EMIT changed();
    }
    void resize(QSizeF size)
    {
        if (model.size() == size) return;
        model.resize(size);
        publish();
    }
    void tap(QPointF p)
    {
        if (model.tap(p, QDateTime::currentMSecsSinceEpoch()) > 0) publish();
    }
    NexusModel model;
    QUrl url;
Q_SIGNALS:
    void changed();
private:
    QTemporaryDir directory;
    QTimer timer;
    quint64 revision = 0;
};

namespace {
QHash<QString, QWeakPointer<NexusCanvas>> canvases;
}

NexusState::NexusState(QObject *parent) : QObject(parent) {}
NexusState::~NexusState()
{
    if (m_window) m_window->removeEventFilter(this);
}

void NexusState::setTarget(QQuickItem *item)
{
    if (m_target == item) return;
    if (m_target) disconnect(m_target, nullptr, this, nullptr);
    m_target = item;
    if (m_target) {
        connect(item, &QQuickItem::windowChanged, this, &NexusState::scheduleUpdate);
        connect(item, &QQuickItem::widthChanged, this, &NexusState::scheduleUpdate);
        connect(item, &QQuickItem::heightChanged, this, &NexusState::scheduleUpdate);
        connect(item, &QObject::destroyed, this, &NexusState::scheduleUpdate);
    }
    Q_EMIT settingsChanged();
    scheduleUpdate();
}

void NexusState::setMouseEnabled(bool value)
{
    if (m_mouseEnabled == value) return;
    m_mouseEnabled = value;
    m_pressed = false;
    Q_EMIT settingsChanged();
}

void NexusState::setShared(bool value)
{
    if (m_shared == value) return;
    m_shared = value;
    Q_EMIT settingsChanged();
    scheduleUpdate();
}

void NexusState::setDesktopPixels(QVector2D value)
{
    if (m_desktopPixels == value) return;
    m_desktopPixels = value;
    Q_EMIT settingsChanged();
    scheduleUpdate();
}

void NexusState::setScreenOffset(QVector2D value)
{
    if (m_screenOffset == value) return;
    m_screenOffset = value;
    Q_EMIT settingsChanged();
}

void NexusState::setPixelRatio(qreal value)
{
    value = value > 0 ? value : 1;
    if (qFuzzyCompare(m_pixelRatio, value)) return;
    m_pixelRatio = value;
    Q_EMIT settingsChanged();
    scheduleUpdate();
}

void NexusState::scheduleUpdate()
{
    if (m_updatePending) return;
    m_updatePending = true;
    QTimer::singleShot(0, this, [this] { m_updatePending = false; updateCanvas(); });
}

void NexusState::updateCanvas()
{
    QQuickWindow *window = m_target ? m_target->window() : nullptr;
    if (m_window != window) {
        if (m_window) m_window->removeEventFilter(this);
        m_window = window;
        if (m_window) m_window->installEventFilter(this);
        m_pressed = false;
    }
    if (!m_target || m_target->width() <= 0 || m_target->height() <= 0) {
        if (m_canvas) disconnect(m_canvas.data(), nullptr, this, nullptr);
        m_canvas.reset();
        Q_EMIT textureChanged();
        return;
    }
    QSizeF size = m_desktopPixels.x() > 0 && m_desktopPixels.y() > 0
        ? QSizeF(m_desktopPixels.x() / m_pixelRatio, m_desktopPixels.y() / m_pixelRatio)
        : QSizeF(m_target->width(), m_target->height());
    QString key = m_shared ? QStringLiteral("desktop") : QString::number(quintptr(this), 16);
    auto canvas = canvases.value(key).toStrongRef();
    if (!canvas) {
        canvas = QSharedPointer<NexusCanvas>::create(size);
        canvases.insert(key, canvas);
    }
    if (m_canvas != canvas) {
        if (m_canvas) disconnect(m_canvas.data(), nullptr, this, nullptr);
        m_canvas = canvas;
        connect(m_canvas.data(), &NexusCanvas::changed, this, &NexusState::textureChanged);
        Q_EMIT textureChanged();
    }
    m_canvas->resize(size);
    refreshTargetShader();
}

void NexusState::refreshTargetShader()
{
    if (!m_target) return;
    QUrl source = m_target->property("shaderSource").toUrl();
    if (!source.isLocalFile()) return;
    QFile input(source.toLocalFile());
    if (!input.open(QIODevice::ReadOnly)) return;
    QByteArray code = input.readAll();
    input.close();
    if (code.isEmpty() || code == m_target->property("shaderCode").toString().toUtf8()) return;
    // Enable the existing engine's own hot-reload path temporarily. C++
    // property writes preserve its QML binding; no source URL or selection
    // changes, and no graphics context is recreated.
    bool wasWatching = m_target->property("watchSourceFile").toBool();
    m_target->setProperty("watchSourceFile", true);
    QSaveFile output(source.toLocalFile());
    if (!output.open(QIODevice::WriteOnly) || output.write(code) != code.size() || !output.commit()) {
        m_target->setProperty("watchSourceFile", wasWatching);
        qWarning("Nexus Classic: cannot refresh the cached shader");
        return;
    }
    QPointer<QQuickItem> target = m_target;
    QTimer::singleShot(600, m_target, [target, wasWatching] {
        if (target) target->setProperty("watchSourceFile", wasWatching);
    });
}

QUrl NexusState::textureUrl() const { return m_canvas ? m_canvas->url : QUrl(); }
int NexusState::extraCount() const { return m_canvas ? m_canvas->model.extraCount() : 0; }

bool NexusState::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window || !m_mouseEnabled || !m_target || !m_canvas || !m_target->isVisible())
        return false;
    auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick
        && type != QEvent::MouseButtonRelease && type != QEvent::MouseMove)
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    QPointF local = m_target->mapFromScene(mouse->scenePosition());
    bool inside = m_target->boundingRect().contains(local);
    if ((type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) && mouse->button() == Qt::LeftButton) {
        m_pressed = inside;
        m_dragged = false;
        m_pressPosition = mouse->scenePosition();
    } else if (type == QEvent::MouseMove && m_pressed) {
        if ((mouse->scenePosition() - m_pressPosition).manhattanLength() > QGuiApplication::styleHints()->startDragDistance())
            m_dragged = true;
    } else if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
        bool tapped = m_pressed && !m_dragged && inside
            && (mouse->scenePosition() - m_pressPosition).manhattanLength() <= QGuiApplication::styleHints()->startDragDistance();
        m_pressed = false;
        if (tapped) {
            QPointF pixels(local.x() * m_pixelRatio,
                (m_target->height() - local.y()) * m_pixelRatio);
            QPointF point;
            if (m_desktopPixels.x() > 0 && m_desktopPixels.y() > 0) {
                point = QPointF((pixels.x() + m_screenOffset.x()) / m_desktopPixels.x() * m_canvas->model.size().width(),
                    (pixels.y() + m_screenOffset.y()) / m_desktopPixels.y() * m_canvas->model.size().height());
            } else point = QPointF(local.x(), m_target->height() - local.y());
            m_canvas->tap(point);
        }
    }
    // Observe the desktop window; leave icons, selection and context menus alone.
    return false;
}

// Existing Plasma wallpaper objects keep their already-compiled QML until
// login/restart. A settings page can attach the same bridge to those objects
// without tearing down the shell or creating another graphics context.
class NexusRuntimeManager : public QObject {
    Q_OBJECT
public:
    NexusRuntimeManager(const QUrl &bridgeUrl, QObject *parent) : QObject(parent)
    {
        engine = new QQmlEngine(this);
        component = new QQmlComponent(engine, bridgeUrl, QQmlComponent::PreferSynchronous, this);
        if (!component->isReady()) {
            qWarning() << "Nexus Classic runtime:" << component->errorString();
            return;
        }
        connect(&timer, &QTimer::timeout, this, &NexusRuntimeManager::scan);
        timer.start(1000);
        scan();
    }
private Q_SLOTS:
    void updateEngines()
    {
        for (QQuickItem *item : std::as_const(watched)) {
            const QUrl source = item->property("shaderSource").toUrl();
            bool nexus = source.path().endsWith("/Shaders/Retro/NexusClassic/main.frag");
            QQuickItem *host = item->parentItem();
            bool nativeBridge = host && host->findChild<QObject *>("nexusClassicBridgeLoader", Qt::FindDirectChildrenOnly);
            bool restricted = host && host->property("restrictedMode").toBool();
            if (!nexus || nativeBridge || restricted) {
                removeBridge(item);
                continue;
            }
            // Fresh ShaderSystem.qml instances already own a bridge.
            bool hasBridge = false;
            if (item->parentItem()) {
                for (QObject *candidate : item->parentItem()->findChildren<QObject *>("nexusClassicBridge")) {
                    if (candidate->property("engine").value<QObject *>() == item) {
                        hasBridge = true;
                        break;
                    }
                }
            }
            if (!bridges.contains(item) && !hasBridge) {
                QVariantMap properties{{"engine", QVariant::fromValue(static_cast<QObject *>(item))},
                    {"mouseEnabled", item->property("mouseEnabled")},
                    {"shared", item->property("screenCount").toInt() > 1}};
                QObject *bridge = component->createWithInitialProperties(properties);
                if (!bridge) {
                    qWarning() << "Nexus Classic runtime bridge:" << component->errorString();
                    continue;
                }
                bridge->setParent(this);
                QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
                bridges.insert(item, bridge);
            }
            if (QObject *bridge = bridges.value(item)) {
                bridge->setProperty("mouseEnabled", item->property("mouseEnabled"));
                bridge->setProperty("shared", item->property("screenCount").toInt() > 1);
            }
        }
    }
    void scan()
    {
        QList<QQuickItem *> pending;
        for (QWindow *window : QGuiApplication::allWindows())
            if (auto quick = qobject_cast<QQuickWindow *>(window)) pending.append(quick->contentItem());
        while (!pending.isEmpty()) {
            QQuickItem *item = pending.takeLast();
            if (!item) continue;
            pending.append(item->childItems());
            if (!item->inherits("ShaderEngine") || watched.contains(item)) continue;
            watched.insert(item);
            connect(item, SIGNAL(shaderSourceChanged()), this, SLOT(updateEngines()));
            connect(item, SIGNAL(mouseEnabledChanged()), this, SLOT(updateEngines()));
            connect(item, SIGNAL(screenCountChanged()), this, SLOT(updateEngines()));
            connect(item, &QObject::destroyed, this, [this, item] {
                watched.remove(item);
                removeBridge(item);
            });
        }
        updateEngines();
    }
private:
    void removeBridge(QQuickItem *item)
    {
        if (QObject *bridge = bridges.take(item)) {
            QMetaObject::invokeMethod(bridge, "detach", Qt::DirectConnection);
            delete bridge;
        }
    }
    QQmlEngine *engine = nullptr;
    QQmlComponent *component = nullptr;
    QTimer timer;
    QSet<QQuickItem *> watched;
    QHash<QQuickItem *, QObject *> bridges;
};

void NexusRuntime::activate(const QUrl &bridgeUrl)
{
    static QPointer<NexusRuntimeManager> runtime;
    if (!runtime && bridgeUrl.isLocalFile()) {
        runtime = new NexusRuntimeManager(bridgeUrl, QGuiApplication::instance());
        qInfo("Nexus Classic: runtime helper attached to existing wallpaper engines");
    }
}

#include "nexusinput.moc"
