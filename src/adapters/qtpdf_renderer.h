#pragma once

#include "application/pdf_renderer.h"

#include <memory>

class QPdfDocument;

namespace misign::adapters {

// Renders with Qt PDF (PDFium), always with annotations: without them a drawn
// signature is not displayed (ADR 0005). Needs a QGuiApplication, and must be
// used from one thread.
class QtPdfRenderer final : public application::IPdfRenderer {
public:
    QtPdfRenderer();
    QtPdfRenderer(const QtPdfRenderer &) = delete;
    QtPdfRenderer &operator=(const QtPdfRenderer &) = delete;
    QtPdfRenderer(QtPdfRenderer &&) = delete;
    QtPdfRenderer &operator=(QtPdfRenderer &&) = delete;
    ~QtPdfRenderer() override;

    [[nodiscard]] std::optional<application::DocumentError>
    open(const std::filesystem::path &file) override;
    void close() override;

    [[nodiscard]] int pageCount() const override;
    [[nodiscard]] application::PageSize pageSize(int page) const override;
    [[nodiscard]] application::PageImage render(int page, double scale) override;

private:
    std::unique_ptr<QPdfDocument> m_document;
};

} // namespace misign::adapters
