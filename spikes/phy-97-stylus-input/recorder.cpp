#include "recorder.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMap>
#include <QMetaEnum>
#include <QMouseEvent>
#include <QPointerEvent>
#include <QPointingDevice>
#include <QSet>
#include <QTabletEvent>
#include <QTextStream>

#include <algorithm>

namespace {

// Names of the flags set in `value`, for an enum or flags type registered in `meta`.
QString keys(const QMetaObject &meta, const char *enumName, int value)
{
    const QMetaEnum metaEnum = meta.enumerator(meta.indexOfEnumerator(enumName));
    return QString::fromLatin1(metaEnum.valueToKeys(value));
}

QString eventName(QEvent::Type type)
{
    return QString::fromLatin1(QMetaEnum::fromType<QEvent::Type>().valueToKey(type));
}

// A mouse event from a real mouse or touchpad, not one Qt synthesized from a
// stylus or a touchscreen.
bool isPointerMouse(const QPointerEvent *event)
{
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseMove &&
        event->type() != QEvent::MouseButtonRelease) {
        return false;
    }
    const QPointingDevice *device = event->pointingDevice();
    return device && (device->type() == QInputDevice::DeviceType::Mouse ||
                      device->type() == QInputDevice::DeviceType::TouchPad);
}

// Contact for everything but real mouse and touchpad events, which toggle.
bool isContact(const QPointerEvent *event, const QEventPoint &point)
{
    switch (event->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
        return static_cast<const QTabletEvent *>(event)->buttons() != Qt::NoButton ||
               point.pressure() > 0.0;
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
        return static_cast<const QSinglePointEvent *>(event)->buttons() != Qt::NoButton;
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
        return point.state() != QEventPoint::Released;
    default:
        return false;
    }
}

// Event class: "Tablet", "Mouse", "Touch", or the event name for the others.
QString eventClass(const QString &event)
{
    for (const auto *prefix : {"Tablet", "Mouse", "Touch"}) {
        if (event.startsWith(QLatin1String(prefix))) {
            return QLatin1String(prefix);
        }
    }
    return event;
}

} // namespace

Recorder::Recorder(QObject *parent) : QObject(parent)
{
    m_clock.start();
}

void Recorder::configure(const QString &outputDir, const QString &mode)
{
    s_outputDir = outputDir;
    s_mode = mode;
}

bool Recorder::eventFilter(QObject *watched, QEvent *event)
{
    if (!event->isPointerEvent()) {
        return QObject::eventFilter(watched, event);
    }
    const auto *pointerEvent = static_cast<const QPointerEvent *>(event);
    const QPointingDevice *device = pointerEvent->pointingDevice();
    const auto *tablet = event->type() == QEvent::TabletPress ||
                                 event->type() == QEvent::TabletMove ||
                                 event->type() == QEvent::TabletRelease
                             ? static_cast<const QTabletEvent *>(event)
                             : nullptr;
    int buttons = 0;
    if (event->isSinglePointEvent()) {
        buttons = static_cast<int>(static_cast<const QSinglePointEvent *>(event)->buttons());
    }
    const bool pointerMouse = isPointerMouse(pointerEvent);
    const QString deviceName = device ? device->name() : QString();
    if (pointerMouse && event->type() == QEvent::MouseButtonRelease &&
        static_cast<const QMouseEvent *>(event)->button() == Qt::LeftButton) {
        m_mouseDrawing[deviceName] = !m_mouseDrawing.value(deviceName);
    }
    for (const QEventPoint &point : pointerEvent->points()) {
        append({
            .elapsedMs = static_cast<double>(m_clock.nsecsElapsed()) / 1e6,
            .eventTime = pointerEvent->timestamp(),
            .source = QStringLiteral("window"),
            .event = eventName(event->type()),
            .device = device ? device->name() : QString(),
            .deviceType = device ? keys(QInputDevice::staticMetaObject, "DeviceType",
                                        static_cast<int>(device->type()))
                                 : QString(),
            .pointerType = device ? keys(QPointingDevice::staticMetaObject, "PointerType",
                                         static_cast<int>(device->pointerType()))
                                  : QString(),
            .capabilities = device ? keys(QInputDevice::staticMetaObject, "Capability",
                                          static_cast<int>(device->capabilities()))
                                   : QString(),
            .position = point.scenePosition(),
            .pressure = point.pressure(),
            .xTilt = tablet ? tablet->xTilt() : 0.0,
            .yTilt = tablet ? tablet->yTilt() : 0.0,
            .rotation = tablet ? tablet->rotation() : 0.0,
            .buttons = buttons,
            .contact =
                pointerMouse ? m_mouseDrawing.value(deviceName) : isContact(pointerEvent, point),
        });
    }
    // Never consume: Qt Quick must still deliver the event, so we also see
    // which mouse events it synthesizes and what the PointHandlers get.
    return false;
}

bool Recorder::anyMouseDrawing() const
{
    return std::any_of(m_mouseDrawing.cbegin(), m_mouseDrawing.cend(),
                       [](bool drawing) { return drawing; });
}

void Recorder::recordHandler(const QString &handler, double x, double y, double pressure,
                             bool pressed)
{
    // The hover handler never has a button pressed: it draws in toggle mode.
    if (handler == QLatin1String("hover")) {
        pressed = anyMouseDrawing();
    }
    append({
        .elapsedMs = static_cast<double>(m_clock.nsecsElapsed()) / 1e6,
        .eventTime = 0,
        .source = QStringLiteral("handler:") + handler,
        .event = pressed ? QStringLiteral("active") : QStringLiteral("hover"),
        .device = QString(),
        .deviceType = QString(),
        .pointerType = QString(),
        .capabilities = QString(),
        .position = {x, y},
        .pressure = pressure,
        .xTilt = 0.0,
        .yTilt = 0.0,
        .rotation = 0.0,
        .buttons = 0,
        .contact = pressed,
    });
}

void Recorder::append(Sample sample)
{
    m_samples.append(std::move(sample));
    emit changed();
}

void Recorder::mark(const QString &label)
{
    Sample sample{};
    sample.elapsedMs = static_cast<double>(m_clock.nsecsElapsed()) / 1e6;
    sample.source = QStringLiteral("marker");
    sample.event = label;
    append(std::move(sample));
}

void Recorder::clear()
{
    m_samples.clear();
    m_mouseDrawing.clear();
    emit changed();
}

QString Recorder::summary() const
{
    struct Group {
        int samples = 0;
        int contact = 0;
        double minPressure = 1.0;
        double maxPressure = 0.0;
        QSet<double> pressures;
        bool tilt = false;
        QSet<QString> events;
        QList<double> intervals;
        double lastContactMs = -1.0;
        QString capabilities;
    };
    QMap<QString, Group> groups;
    for (const Sample &s : m_samples) {
        // Split by event class too: Qt sends mouse events synthesized from the
        // stylus with the stylus device, microseconds after the tablet event.
        const QString key = s.source + QStringLiteral(" | ") +
                            (s.device.isEmpty() ? QStringLiteral("-") : s.device) +
                            QStringLiteral(" | ") + s.deviceType + QStringLiteral("/") +
                            s.pointerType + QStringLiteral(" | ") + eventClass(s.event);
        Group &g = groups[key];
        ++g.samples;
        g.events.insert(s.event);
        g.capabilities = s.capabilities;
        if (s.xTilt != 0.0 || s.yTilt != 0.0) {
            g.tilt = true;
        }
        if (!s.contact) {
            g.lastContactMs = -1.0;
            continue;
        }
        ++g.contact;
        g.minPressure = std::min(g.minPressure, s.pressure);
        g.maxPressure = std::max(g.maxPressure, s.pressure);
        g.pressures.insert(s.pressure);
        if (g.lastContactMs >= 0.0) {
            g.intervals.append(s.elapsedMs - g.lastContactMs);
        }
        g.lastContactMs = s.elapsedMs;
    }

    QString text;
    QTextStream out(&text);
    out << "Platform: " << QGuiApplication::platformName() << ", mode: " << s_mode << "\n";
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        const Group &g = it.value();
        QStringList events(g.events.cbegin(), g.events.cend());
        events.sort();
        // Events arrive in bursts when the GUI thread is busy, so a median gap
        // means little: report the mean rate over drawing time and the longest gap.
        double drawingMs = 0.0;
        double longestGap = 0.0;
        for (const double gap : g.intervals) {
            drawingMs += gap;
            longestGap = std::max(longestGap, gap);
        }
        out << "\n" << it.key() << "\n";
        out << "  samples " << g.samples << ", in contact " << g.contact << "\n";
        out << "  events: " << events.join(QStringLiteral(", ")) << "\n";
        if (!g.capabilities.isEmpty()) {
            out << "  capabilities: " << g.capabilities << "\n";
        }
        if (g.contact > 0) {
            out << "  pressure " << g.minPressure << " .. " << g.maxPressure << " ("
                << g.pressures.size() << " distinct values)" << (g.tilt ? ", tilt reported" : "")
                << "\n";
            if (drawingMs > 0.0) {
                out << "  ~" << qRound(static_cast<double>(g.intervals.size()) * 1000.0 / drawingMs)
                    << " events/s while drawing, longest gap " << qRound(longestGap) << " ms\n";
            }
        }
    }
    return text;
}

QString Recorder::save()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString base = QDir(s_outputDir)
                             .filePath(QStringLiteral("phy97-%1-%2-%3")
                                           .arg(QGuiApplication::platformName(), s_mode, stamp));
    QFile csv(base + QStringLiteral(".csv"));
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QStringLiteral("cannot write ") + csv.fileName();
    }
    QTextStream out(&csv);
    out << "elapsed_ms,event_time,source,event,device,device_type,pointer_type,capabilities,"
           "x,y,pressure,x_tilt,y_tilt,rotation,buttons,contact\n";
    for (const Sample &s : m_samples) {
        out << QString::number(s.elapsedMs, 'f', 3) << ',' << s.eventTime << ',' << s.source << ','
            << s.event << ",\"" << s.device << "\"," << s.deviceType << ',' << s.pointerType
            << ",\"" << s.capabilities << "\"," << s.position.x() << ',' << s.position.y() << ','
            << s.pressure << ',' << s.xTilt << ',' << s.yTilt << ',' << s.rotation << ','
            << s.buttons << ',' << (s.contact ? 1 : 0) << '\n';
    }
    QFile summaryFile(base + QStringLiteral(".txt"));
    if (summaryFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(&summaryFile) << summary();
    }
    return csv.fileName();
}
