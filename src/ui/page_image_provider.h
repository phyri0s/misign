#pragma once

#include "application/pdf_renderer.h"

#include <QQuickImageProvider>

namespace misign::ui {

// Serves "image://pages/<generation>/<page>" (page numbered from 0), rendered
// at the pixel size the Image asks for: its `sourceSize` multiplied by the
// device pixel ratio, which Qt Quick applies itself. Without a requested size
// the page is rendered at 1 pixel per point.
//
// The Image must not be asynchronous: the renderer is used from the GUI thread.
class PageImageProvider : public QQuickImageProvider {
public:
    explicit PageImageProvider(application::IPdfRenderer &renderer);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    static constexpr auto kProviderId = "pages";

private:
    application::IPdfRenderer &m_renderer;
};

} // namespace misign::ui
