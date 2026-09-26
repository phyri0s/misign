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

void Recorder::configure(const QString &outputDir, const QString &mode, bool acceptTablet)
{
    s_outputDir = outputDir;
    s_mode = mode;
    s_acceptTablet = acceptTablet;
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
            .globalPosition = point.globalPosition(),
            .pressure = point.pressure(),
            .xTilt = tablet ? tablet->xTilt() : 0.0,
            .yTilt = tablet ? tablet->yTilt() : 0.0,
            .rotation = tablet ? tablet->rotation() : 0.0,
            .buttons = buttons,
            .contact =
                pointerMouse ? m_mouseDrawing.value(deviceName) : isContact(pointerEvent, point),
        });
    }
    // Qt synthesizes a mouse event from every tablet event nobody accepted
    // (QGuiApplicationPrivate::processTabletEvent). By default the probe does
    // not consume, so Qt Quick still delivers the event and we see those mouse
    // twins and what the handlers get; --accept-tablet behaves like Misign's
    // capture code will.
    if (tablet && s_acceptTablet) {
        event->accept();
        return true;
    }
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
        .globalPosition = {},
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
    if (sample.source != QLatin1String("marker")) {
        // Split by event class too: unless tablet events are accepted, Qt sends
        // a mouse event synthesized from each one, with the stylus device.
        const QString key = sample.source + QStringLiteral(" | ") +
                            (sample.device.isEmpty() ? QStringLiteral("-") : sample.device) +
                            QStringLiteral(" | ") + sample.deviceType + QStringLiteral("/") +
                            sample.pointerType + QStringLiteral(" | ") + eventClass(sample.event);
        Group &g = m_groups[key];
        ++g.samples;
        g.events.insert(sample.event);
        g.capabilities = sample.capabilities;
        g.tilt = g.tilt || sample.xTilt != 0.0 || sample.yTilt != 0.0;
        if (sample.contact) {
            ++g.contact;
            g.minPressure = std::min(g.minPressure, sample.pressure);
            g.maxPressure = std::max(g.maxPressure, sample.pressure);
            g.pressures.insert(sample.pressure);
            if (g.lastContactMs >= 0.0) {
                const double gap = sample.elapsedMs - g.lastContactMs;
                ++g.intervals;
                g.drawingMs += gap;
                g.longestGap = std::max(g.longestGap, gap);
            }
            g.lastContactMs = sample.elapsedMs;
        } else {
            g.lastContactMs = -1.0;
        }
    }
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
    m_groups.clear();
    m_mouseDrawing.clear();
    emit changed();
}

QString Recorder::summary() const
{
    QString text;
    QTextStream out(&text);
    out << "Platform: " << QGuiApplication::platformName() << ", mode: " << s_mode << "\n";
    for (auto it = m_groups.cbegin(); it != m_groups.cend(); ++it) {
        const Group &g = it.value();
        QStringList events(g.events.cbegin(), g.events.cend());
        events.sort();
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
            // Events arrive in bursts when the GUI thread is busy, so a median
            // gap means little: the mean rate over drawing time and the longest gap.
            if (g.drawingMs > 0.0) {
                out << "  ~" << qRound(static_cast<double>(g.intervals) * 1000.0 / g.drawingMs)
                    << " events/s while drawing, longest gap " << qRound(g.longestGap) << " ms\n";
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
           "x,y,global_x,global_y,pressure,x_tilt,y_tilt,rotation,buttons,contact\n";
    for (const Sample &s : m_samples) {
        out << QString::number(s.elapsedMs, 'f', 3) << ',' << s.eventTime << ',' << s.source << ','
            << s.event << ",\"" << s.device << "\"," << s.deviceType << ',' << s.pointerType
            << ",\"" << s.capabilities << "\"," << QString::number(s.position.x(), 'f', 3) << ','
            << QString::number(s.position.y(), 'f', 3) << ','
            << QString::number(s.globalPosition.x(), 'f', 3) << ','
            << QString::number(s.globalPosition.y(), 'f', 3) << ',' << s.pressure << ',' << s.xTilt
            << ',' << s.yTilt << ',' << s.rotation << ',' << s.buttons << ',' << (s.contact ? 1 : 0)
            << '\n';
    }
    QFile summaryFile(base + QStringLiteral(".txt"));
    if (summaryFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream(&summaryFile) << summary();
    }
    return csv.fileName();
}
