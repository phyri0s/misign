#pragma once

#include "recorder.h"

#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Draws the raw window samples in contact, one polyline per stroke and device,
// with a width following the pressure: what a naive renderer would get.
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

private:
    Recorder *m_recorder = nullptr;
    bool m_showPoints = true;
};
