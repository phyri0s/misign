#pragma once

#include "application/document_error.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace misign::application {

// Size of a page as displayed (its visible area turned by /Rotate), in points.
// 0 x 0 for a page without a visible area.
struct PageSize {
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] bool isEmpty() const noexcept { return width <= 0.0 || height <= 0.0; }
};

// A rendered page: `height` rows of `width` opaque pixels, each 0xFFRRGGBB.
struct PageImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;

    [[nodiscard]] bool isEmpty() const noexcept { return pixels.empty(); }
};

// Displays the pages of one document at a time, as a viewer shows them:
// upright, annotations included, so a drawn signature is visible.
class IPdfRenderer {
public:
    IPdfRenderer() = default;
    IPdfRenderer(const IPdfRenderer &) = delete;
    IPdfRenderer &operator=(const IPdfRenderer &) = delete;
    IPdfRenderer(IPdfRenderer &&) = delete;
    IPdfRenderer &operator=(IPdfRenderer &&) = delete;
    virtual ~IPdfRenderer() = default;

    // Replaces the open document. On failure no document is open.
    [[nodiscard]] virtual std::optional<DocumentError> open(const std::filesystem::path &file) = 0;
    virtual void close() = 0;

    // 0 when no document is open.
    [[nodiscard]] virtual int pageCount() const = 0;
    // Pages are numbered from 0. Empty for a page that does not exist.
    [[nodiscard]] virtual PageSize pageSize(int page) const = 0;
    // The page on a white background at `scale` pixels per point (device pixel
    // ratio included), its size rounded to whole pixels. Empty for a page that
    // does not exist or has no visible area, or when scale <= 0.
    [[nodiscard]] virtual PageImage render(int page, double scale) = 0;
};

} // namespace misign::application
