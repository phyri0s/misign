#pragma once

#include "recorder.h"

#include <QHash>
#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Draws the raw window samples while drawing (stylus in contact, or mouse and
// touchpad between two clicks), one polyline per stroke and device, with a
// width following the pressure: what a naive renderer would get.
//
// Samples are drawn incrementally into a cached image, so the cost per frame
// stays constant and the GUI thread stays free: a busy GUI thread makes Qt
// compress, and Windows coalesce, the input events being measured.
class InkView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Recorder *recorder READ recorder WRITE setRecorder NOTIFY recorderChanged)
    Q_PROPERTY(bool showPoints READ showPoints WRITE setShowPoints NOTIFY showPointsChanged)

public:
    explicit InkView(QQuickItem *parent = nullptr);

    [[nodiscard]] Recorder *recorder() const { return m_recorder; }
    void setRecorder(Recorder *recorder);
    [[nodiscard]] bool showPoints() const { return m_showPoints; }
    void setShowPoints(bool show);

    void paint(QPainter *painter) override;

signals:
    void recorderChanged();
    void showPointsChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void drawNewSamples();
    void redrawAll();

    Recorder *m_recorder = nullptr;
    bool m_showPoints = true;
    QImage m_canvas;
    qsizetype m_drawn = 0;          // Samples already drawn into the canvas.
    QHash<QString, QPointF> m_last; // Last drawing point per device.
};
