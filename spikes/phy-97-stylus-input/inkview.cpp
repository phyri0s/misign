#include "inkview.h"

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
        connect(m_recorder, &Recorder::changed, this, &InkView::drawNewSamples);
    }
    emit recorderChanged();
    redrawAll();
}

void InkView::setShowPoints(bool show)
{
    if (show != m_showPoints) {
        m_showPoints = show;
        emit showPointsChanged();
        redrawAll();
    }
}

void InkView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        redrawAll();
    }
}

void InkView::redrawAll()
{
    const QSize size = boundingRect().size().toSize();
    m_canvas = size.isEmpty() ? QImage() : QImage(size, QImage::Format_ARGB32_Premultiplied);
    m_canvas.fill(Qt::transparent);
    m_drawn = 0;
    m_last.clear();
    drawNewSamples();
}

void InkView::drawNewSamples()
{
    if (!m_recorder || m_canvas.isNull()) {
        return;
    }
    const QList<Sample> &samples = m_recorder->samples();
    if (samples.size() < m_drawn) { // Cleared.
        redrawAll();
        return;
    }
    QPainter painter(&m_canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    for (; m_drawn < samples.size(); ++m_drawn) {
        const Sample &s = samples.at(m_drawn);
        if (s.source != QLatin1String("window")) {
            continue;
        }
        // Synthesized mouse events are drawn too, in grey, to show whether
        // they duplicate the tablet stream.
        const QString key = s.device + s.deviceType + s.event.left(5);
        if (!s.contact) {
            m_last.remove(key);
            continue;
        }
        const bool isTablet = s.event.startsWith(QLatin1String("Tablet"));
        const bool isTouch = s.event.startsWith(QLatin1String("Touch"));
        const QColor colour = isTablet  ? QColor(20, 40, 160)
                              : isTouch ? QColor(0, 130, 60)
                                        : QColor(150, 150, 150);
        const double width = 0.5 + (s.pressure * 8.0);
        const auto it = m_last.constFind(key);
        if (it != m_last.cend()) {
            painter.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(*it, s.position);
        }
        if (m_showPoints) {
            painter.setPen(QPen(Qt::red, 2.0));
            painter.drawPoint(s.position);
        }
        m_last.insert(key, s.position);
    }
    update();
}

void InkView::paint(QPainter *painter)
{
    if (!m_canvas.isNull()) {
        painter->drawImage(0, 0, m_canvas);
    }
}
