// PHY-97 spike: logs what Qt 6.11 delivers for stylus, touchpad and mouse input.
//
//   stylus-probe [--no-compress] [--out <dir>] [--quit-after <ms>] [Qt options]
//
// --no-compress turns off Qt's compression of tablet and high-frequency events,
// to see the full point stream. On Windows, `-platform windows:nowmpointer`
// switches from Windows Ink (WM_POINTER) to Wintab.

#include "recorder.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

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
    parser.addOption({QStringLiteral("out"), QStringLiteral("Directory for the logs"),
                      QStringLiteral("dir"), QDir::currentPath()});
    parser.addOption({QStringLiteral("quit-after"), QStringLiteral("Save and quit after <ms>"),
                      QStringLiteral("ms")});
    parser.process(app);

    QString mode = compress ? QStringLiteral("compressed") : QStringLiteral("uncompressed");
    const QString platformArgument = qEnvironmentVariable("QT_QPA_PLATFORM");
    if (platformArgument.contains(QLatin1Char(':'))) {
        mode += QLatin1Char('-') + platformArgument.section(QLatin1Char(':'), 1);
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
