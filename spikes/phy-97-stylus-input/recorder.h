#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QtQml/qqmlregistration.h>

// One input sample, as delivered to the window or to a QML PointHandler.
struct Sample {
    double elapsedMs;  // Since the recorder started (steady clock, sub-millisecond).
    quint64 eventTime; // QEvent timestamp (ms), 0 for PointHandler samples.
    QString source;    // "window" or "handler:<name>".
    QString event;     // e.g. TabletMove, MouseButtonPress, TouchUpdate.
    QString device;
    QString deviceType;
    QString pointerType;
    QString capabilities;
    QPointF position; // Scene coordinates.
    double pressure;
    double xTilt;
    double yTilt;
    double rotation;
    int buttons;
    bool contact; // Pen, button or finger down.
};

// Records every pointer event reaching the window (installed as an event
// filter, so it sees events before Qt Quick delivers them), plus what QML
// handlers report. Writes a CSV log and a per-device summary.
//
// "Contact" means drawing: the stylus draws while it touches the surface; the
// mouse and the touchpad draw between a click that starts drawing and a click
// that stops it, with no button held.
class Recorder : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString mode READ mode CONSTANT)

public:
    explicit Recorder(QObject *parent = nullptr);

    static void configure(const QString &outputDir, const QString &mode);

    // Recomputed on each call: QML polls it on a timer rather than binding to it.
    Q_INVOKABLE [[nodiscard]] QString summary() const;
    [[nodiscard]] QString mode() const { return s_mode; }
    [[nodiscard]] const QList<Sample> &samples() const { return m_samples; }

    Q_INVOKABLE void recordHandler(const QString &handler, double x, double y, double pressure,
                                   bool pressed);
    Q_INVOKABLE void clear();
    // Writes the CSV log and the summary; returns the log path.
    Q_INVOKABLE QString save();

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void changed();

private:
    void append(Sample sample);

    [[nodiscard]] bool anyMouseDrawing() const;

    QElapsedTimer m_clock;
    QList<Sample> m_samples;
    // Mouse and touchpad draw as a toggle: a click starts drawing, the next
    // click stops it. Per device name.
    QHash<QString, bool> m_mouseDrawing;
    static inline QString s_outputDir;
    static inline QString s_mode;
};
