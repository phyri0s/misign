// PHY-97 spike: logs what Qt 6.11 delivers for stylus, touchpad and mouse input.
//
//   stylus-probe [--no-compress] [--wintab] [--out <dir>] [--quit-after <ms>]
//
// --no-compress turns off Qt's compression of tablet and high-frequency events,
// to see the full point stream. --wintab (Windows) switches from Windows Ink
// (WM_POINTER, Qt's default) to Wintab. Qt 6.11 has no platform option for it
// any more ("windows:nowmpointer" is reported as unknown): only the private
// QWindowsApplication::setWinTabEnabled does it.

#include "recorder.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

#ifdef Q_OS_WIN
#include <QtGui/private/qguiapplication_p.h>
#endif

int main(int argc, char *argv[])
{
    // Compression must be decided before the application object exists.
    bool compress = true;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--no-compress") == 0) {
            compress = false;
        }
    }
    QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, compress);
    QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, compress);

    QGuiApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("no-compress"), QStringLiteral("Deliver every input event")});
    parser.addOption({QStringLiteral("wintab"), QStringLiteral("Use Wintab (Windows)")});
    parser.addOption({QStringLiteral("out"), QStringLiteral("Directory for the logs"),
                      QStringLiteral("dir"), QDir::currentPath()});
    parser.addOption({QStringLiteral("quit-after"), QStringLiteral("Save and quit after <ms>"),
                      QStringLiteral("ms")});
    parser.process(app);

    QString mode = compress ? QStringLiteral("compressed") : QStringLiteral("uncompressed");
    if (parser.isSet(QStringLiteral("wintab"))) {
        bool enabled = false;
#ifdef Q_OS_WIN
        using QNativeInterface::Private::QWindowsApplication;
        if (auto *windows = app.nativeInterface<QWindowsApplication>()) {
            enabled = windows->setWinTabEnabled(true) && windows->isWinTabEnabled();
        }
#endif
        mode += enabled ? QStringLiteral("-wintab") : QStringLiteral("-wintab-unavailable");
    }
    Recorder::configure(parser.value(QStringLiteral("out")), mode);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("StylusProbe", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    auto *recorder = engine.singletonInstance<Recorder *>("StylusProbe", "Recorder");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    window->installEventFilter(recorder);

    QObject::connect(&app, &QGuiApplication::aboutToQuit, recorder, [recorder] {
        if (!recorder->samples().isEmpty()) {
            qInfo("log written to %s", qPrintable(recorder->save()));
        }
    });
    if (parser.isSet(QStringLiteral("quit-after"))) {
        QTimer::singleShot(parser.value(QStringLiteral("quit-after")).toInt(), &app,
                           &QGuiApplication::quit);
    }
    return QGuiApplication::exec();
}
