#include "inkview.h"

#include "recorder.h"

#include <QHash>
#include <QPainter>

InkView::InkView(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void InkView::setRecorder(Recorder *recorder)
{
    if (recorder == m_recorder) {
        return;
    }
    if (m_recorder) {
        disconnect(m_recorder, nullptr, this, nullptr);
    }
    m_recorder = recorder;
    if (m_recorder) {
        connect(m_recorder, &Recorder::changed, this, [this] { update(); });
    }
    emit recorderChanged();
    update();
}

void InkView::setShowPoints(bool show)
{
    if (show != m_showPoints) {
        m_showPoints = show;
        emit showPointsChanged();
        update();
    }
}

void InkView::paint(QPainter *painter)
{
    if (!m_recorder) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing);
    // Last contact point per device: consecutive contact samples of the same
    // device form a stroke. Synthesized mouse events are drawn too, in grey,
    // to show whether they duplicate the tablet stream.
    QHash<QString, QPointF> last;
    for (const Sample &s : m_recorder->samples()) {
        if (s.source != QLatin1String("window")) {
            continue;
        }
        const QString key = s.device + s.deviceType + s.event.left(5);
        if (!s.contact) {
            last.remove(key);
            continue;
        }
        const bool isTablet = s.event.startsWith(QLatin1String("Tablet"));
        const bool isTouch = s.event.startsWith(QLatin1String("Touch"));
        const QColor colour = isTablet  ? QColor(20, 40, 160)
                              : isTouch ? QColor(0, 130, 60)
                                        : QColor(150, 150, 150);
        const double width = 0.5 + (s.pressure * 8.0);
        const auto it = last.constFind(key);
        if (it != last.cend()) {
            painter->setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(*it, s.position);
        }
        if (m_showPoints) {
            painter->setPen(QPen(Qt::red, 2.0));
            painter->drawPoint(s.position);
        }
        last.insert(key, s.position);
    }
}
