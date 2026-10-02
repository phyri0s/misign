#include "adapters/qtpdf_renderer.h"
#include "ui/document_model.h"
#include "ui/page_image_provider.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Misign"));
    QGuiApplication::setOrganizationName(QStringLiteral("Misign"));
    QGuiApplication::setApplicationVersion(QStringLiteral(MISIGN_VERSION));

    misign::adapters::QtPdfRenderer renderer;
    misign::ui::DocumentModel document(renderer);
    // Until documents are opened from the interface: the file given as the
    // first argument is displayed.
    const QStringList arguments = QGuiApplication::arguments();
    if (arguments.size() > 1 && document.open(arguments.at(1))) {
        qWarning("Cannot open %s", qPrintable(arguments.at(1)));
    }

    QQmlApplicationEngine engine;
    // The engine owns the provider.
    engine.addImageProvider(QLatin1String(misign::ui::PageImageProvider::kProviderId),
                            new misign::ui::PageImageProvider(renderer));
    engine.setInitialProperties({{QStringLiteral("document"), QVariant::fromValue(&document)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("Misign", "Main");

    return QGuiApplication::exec();
}
