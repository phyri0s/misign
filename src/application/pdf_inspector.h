#pragma once

#include "domain/page_geometry.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <variant>
#include <vector>

namespace misign::application {

// What a page says about itself, as written in the file once page tree
// inheritance is resolved. /UserUnit is neither read nor exposed (ADR 0010).
struct PageInfo {
    // Corners as written, in any order. Empty when the page has no MediaBox.
    domain::PdfRect mediaBox{};
    // Absent when neither the page nor the page tree sets a CropBox, or when
    // it is not four numbers.
    std::optional<domain::PdfRect> cropBox;
    // /Rotate in degrees as written, e.g. -90 or 450; 0 when absent or too
    // large for an int.
    int rotate = 0;
    // Rectangles of the signature field widgets on this page, signed or not,
    // normalized. An invisible signature has an empty rectangle; a widget
    // whose /Rect cannot be read is left out.
    std::vector<domain::PdfRect> signatureWidgets;

    // The page as a viewer displays it.
    [[nodiscard]] domain::PageGeometry geometry() const noexcept
    {
        return {mediaBox, cropBox, domain::rotationFromDegrees(rotate)};
    }
};

// What the author of a certified document allows to change (DocMDP, /P).
enum class CertificationLevel : std::uint8_t {
    NoChanges = 1,
    FormFillingAndSigning = 2,
    // Also annotations: the only level that lets a drawn signature be added.
    Annotations = 3,
};

struct DocumentInfo {
    std::vector<PageInfo> pages;
    // Absent when the document is not certified (no /Perms /DocMDP).
    std::optional<CertificationLevel> certification;
};

// Why a document could not be inspected.
enum class InspectionError : std::uint8_t {
    // The file does not exist or cannot be read.
    Unreadable,
    NotAPdf,
    // A PDF whose structure cannot be read, e.g. a truncated file.
    Damaged,
    PasswordProtected,
};

using InspectionResult = std::variant<DocumentInfo, InspectionError>;

// Reads what placement and saving depend on and Qt PDF does not expose. The
// file is only read, never modified. A page entry that cannot be read does
// not fail the document: it is read as absent. The exception is a MediaBox
// that is not four numbers, which makes the document Damaged. Never throws.
class IPdfInspector {
public:
    IPdfInspector() = default;
    IPdfInspector(const IPdfInspector &) = delete;
    IPdfInspector &operator=(const IPdfInspector &) = delete;
    IPdfInspector(IPdfInspector &&) = delete;
    IPdfInspector &operator=(IPdfInspector &&) = delete;
    virtual ~IPdfInspector() = default;

    [[nodiscard]] virtual InspectionResult inspect(const std::filesystem::path &file) const = 0;
};

} // namespace misign::application
