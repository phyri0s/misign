#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QSet>
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
    QPointF position;       // Scene coordinates (whole pixels for Windows Ink pens).
    QPointF globalPosition; // Screen coordinates, sub-pixel when the platform has them.
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

    // acceptTablet: accept and consume tablet events, as Misign's capture code
    // will; Qt then synthesizes no mouse event from them.
    static void configure(const QString &outputDir, const QString &mode, bool acceptTablet);

    // Recomputed on each call: QML polls it on a timer rather than binding to it.
    Q_INVOKABLE [[nodiscard]] QString summary() const;
    [[nodiscard]] QString mode() const { return s_mode; }
    [[nodiscard]] const QList<Sample> &samples() const { return m_samples; }

    Q_INVOKABLE void recordHandler(const QString &handler, double x, double y, double pressure,
                                   bool pressed);
    Q_INVOKABLE void clear();
    // Records a marker in the log, e.g. "touchpad" before touchpad input: Qt
    // reports the mouse and the touchpad as the same device on Windows.
    Q_INVOKABLE void mark(const QString &label);
    // Writes the CSV log and the summary; returns the log path.
    Q_INVOKABLE QString save();

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void changed();

private:
    void append(Sample sample);

    [[nodiscard]] bool anyMouseDrawing() const;

    // Running statistics per source, device and event class, kept up to date
    // in append() so that summary() costs the same however long the session.
    struct Group {
        int samples = 0;
        int contact = 0;
        double minPressure = 1.0;
        double maxPressure = 0.0;
        QSet<double> pressures; // At most 1024 values through Windows Ink.
        bool tilt = false;
        QSet<QString> events;
        int intervals = 0;
        double drawingMs = 0.0;
        double longestGap = 0.0;
        double lastContactMs = -1.0;
        QString capabilities;
    };
    QMap<QString, Group> m_groups;

    QElapsedTimer m_clock;
    QList<Sample> m_samples;
    // Mouse and touchpad draw as a toggle: a click starts drawing, the next
    // click stops it. Per device name.
    QHash<QString, bool> m_mouseDrawing;
    static inline QString s_outputDir;
    static inline QString s_mode;
    static inline bool s_acceptTablet = false;
};
