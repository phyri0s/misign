#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Misign"));
    QGuiApplication::setOrganizationName(QStringLiteral("Misign"));
    QGuiApplication::setApplicationVersion(QStringLiteral(MISIGN_VERSION));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("Misign", "Main");

    return QGuiApplication::exec();
}
