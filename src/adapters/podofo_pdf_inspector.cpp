#include "adapters/podofo_pdf_inspector.h"

#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <optional>
#include <podofo/podofo.h>
#include <string>
#include <string_view>
#include <vector>

namespace misign::adapters {

namespace {

using application::CertificationLevel;
using application::DocumentInfo;
using application::InspectionError;
using application::PageInfo;
using domain::PdfRect;

PdfRect asWritten(const PoDoFo::Corners &corners)
{
    return {corners.X1, corners.Y1, corners.X2, corners.Y2};
}

// What `read` returns, or nothing when PoDoFo cannot read it. One malformed
// entry, such as a box that is not four numbers, must not make the whole
// document unreadable: viewers display such files.
template<typename Read>
auto readOrNothing(Read read) -> std::optional<decltype(read())>
{
    try {
        return read();
    } catch (const PoDoFo::PdfError &) {
        return std::nullopt;
    }
}

// PoDoFo's GetCropBoxRaw gives the MediaBox when there is no CropBox, so
// whether one is set, on the page or in the page tree, is read here.
bool hasCropBox(const PoDoFo::PdfPage &page)
{
    const PoDoFo::PdfObject *box = page.GetDictionary().FindKeyParent("CropBox");
    return box != nullptr && box->IsArray();
}

// /Rotate as written. A value that does not fit an int is read as 0.
int readRotate(const PoDoFo::PdfPage &page)
{
    double rotate = 0.0;
    page.TryGetRotationRaw(rotate);
    // The upper bound is exclusive: it is the first double above every int.
    constexpr double kLimit = 2147483648.0;
    static_assert(std::numeric_limits<int>::max() == 2147483647);
    if (rotate > -kLimit && rotate < kLimit) {
        return static_cast<int>(rotate);
    }
    return 0;
}

const PoDoFo::PdfDictionary *findDictionary(const PoDoFo::PdfDictionary &parent,
                                            std::string_view key)
{
    const PoDoFo::PdfObject *object = parent.FindKey(key);
    const PoDoFo::PdfDictionary *dictionary = nullptr;
    if (object == nullptr || !object->TryGetDictionary(dictionary)) {
        return nullptr;
    }
    return dictionary;
}

bool hasName(const PoDoFo::PdfObject *object, std::string_view expected)
{
    const PoDoFo::PdfName *name = nullptr;
    return object != nullptr && object->TryGetName(name) && *name == expected;
}

// The /Rect of the annotation when it is the widget of a signature field.
// The field type may sit on a parent field, which FindKeyParent follows.
std::optional<PdfRect> signatureWidgetRect(const PoDoFo::PdfDictionary &annotation)
{
    if (!hasName(annotation.FindKey("Subtype"), "Widget") ||
        !hasName(annotation.FindKeyParent("FT"), "Sig")) {
        return std::nullopt;
    }
    const PoDoFo::PdfObject *rect = annotation.FindKey("Rect");
    const PoDoFo::PdfArray *array = nullptr;
    if (rect == nullptr || !rect->TryGetArray(array)) {
        return std::nullopt;
    }
    const PoDoFo::Corners corners = PoDoFo::Corners::FromArray(*array);
    return PdfRect::fromCorners({corners.X1, corners.Y1}, {corners.X2, corners.Y2});
}

// Read from the /Annots array itself: PoDoFo's annotation objects throw for
// every entry they cannot model (no /Subtype, an unknown one, a null), which
// has nothing to do with signatures. A signature widget that cannot be read,
// e.g. without a /Rect, is left out.
std::vector<PdfRect> readSignatureWidgets(const PoDoFo::PdfPage &page)
{
    std::vector<PdfRect> widgets;
    const PoDoFo::PdfObject *annotations = page.GetDictionary().FindKey("Annots");
    const PoDoFo::PdfArray *array = nullptr;
    if (annotations == nullptr || !annotations->TryGetArray(array)) {
        return widgets;
    }
    for (unsigned index = 0; index < array->GetSize(); ++index) {
        const std::optional<std::optional<PdfRect>> rect =
            readOrNothing([&]() -> std::optional<PdfRect> {
                const PoDoFo::PdfObject *entry = array->FindAt(index);
                const PoDoFo::PdfDictionary *annotation = nullptr;
                if (entry == nullptr || !entry->TryGetDictionary(annotation)) {
                    return std::nullopt;
                }
                return signatureWidgetRect(*annotation);
            });
        if (rect && *rect) {
            widgets.push_back(**rect);
        }
    }
    return widgets;
}

PageInfo readPage(const PoDoFo::PdfPage &page)
{
    PageInfo info;
    // Cannot throw here: PoDoFo already read the MediaBox to build the page,
    // and a document whose MediaBox is not four numbers fails in GetPageAt.
    info.mediaBox = asWritten(page.GetMediaBoxRaw());
    if (hasCropBox(page)) {
        info.cropBox = readOrNothing([&] { return asWritten(page.GetCropBoxRaw()); });
    }
    info.rotate = readRotate(page);
    info.signatureWidgets = readSignatureWidgets(page);
    return info;
}

// The /P of the DocMDP transform parameters of the certification signature
// (/Perms /DocMDP in the catalog). The PDF specification gives 2 when /P is
// absent or not one of 1, 2, 3.
std::optional<CertificationLevel> readCertification(const PoDoFo::PdfMemDocument &document)
{
    const PoDoFo::PdfDictionary *perms =
        findDictionary(document.GetCatalog().GetDictionary(), "Perms");
    const PoDoFo::PdfDictionary *signature =
        perms != nullptr ? findDictionary(*perms, "DocMDP") : nullptr;
    if (signature == nullptr) {
        return std::nullopt;
    }
    const PoDoFo::PdfObject *references = signature->FindKey("Reference");
    const PoDoFo::PdfArray *array = nullptr;
    if (references == nullptr || !references->TryGetArray(array)) {
        return CertificationLevel::FormFillingAndSigning;
    }
    for (unsigned index = 0; index < array->GetSize(); ++index) {
        const PoDoFo::PdfObject *entry = array->FindAt(index);
        const PoDoFo::PdfDictionary *reference = nullptr;
        if (entry == nullptr || !entry->TryGetDictionary(reference)) {
            continue;
        }
        const PoDoFo::PdfObject *method = reference->FindKey("TransformMethod");
        const PoDoFo::PdfName *name = nullptr;
        if (method == nullptr || !method->TryGetName(name) || *name != "DocMDP") {
            continue;
        }
        const PoDoFo::PdfDictionary *params = findDictionary(*reference, "TransformParams");
        const PoDoFo::PdfObject *level = params != nullptr ? params->FindKey("P") : nullptr;
        std::int64_t value = 0;
        if (level != nullptr && level->TryGetNumber(value) && value >= 1 && value <= 3) {
            return static_cast<CertificationLevel>(value);
        }
        break;
    }
    return CertificationLevel::FormFillingAndSigning;
}

// PoDoFo reports a file without a PDF header as InvalidPDF, and a wrong or
// missing password as InvalidPassword. Any other failure while parsing means
// the PDF structure cannot be read.
InspectionError errorOf(PoDoFo::PdfErrorCode code)
{
    switch (code) {
    case PoDoFo::PdfErrorCode::FileNotFound:
    case PoDoFo::PdfErrorCode::IOError:
        return InspectionError::Unreadable;
    case PoDoFo::PdfErrorCode::InvalidPDF:
        return InspectionError::NotAPdf;
    case PoDoFo::PdfErrorCode::InvalidPassword:
        return InspectionError::PasswordProtected;
    default:
        return InspectionError::Damaged;
    }
}

} // namespace

application::InspectionResult PodofoPdfInspector::inspect(const std::filesystem::path &file) const
{
    // Checked first: PoDoFo does not tell a missing file from a bad one reliably.
    if (!std::ifstream(file, std::ios::binary).good()) {
        return InspectionError::Unreadable;
    }
    try {
        // PoDoFo takes UTF-8 file names on every platform.
        const std::u8string utf8 = file.u8string();
        PoDoFo::PdfMemDocument document;
        document.Load(std::string(utf8.begin(), utf8.end()));

        DocumentInfo info;
        const PoDoFo::PdfPageCollection &pages = document.GetPages();
        info.pages.reserve(pages.GetCount());
        for (unsigned index = 0; index < pages.GetCount(); ++index) {
            info.pages.push_back(readPage(pages.GetPageAt(index)));
        }
        info.certification = readCertification(document);
        return info;
    } catch (const PoDoFo::PdfError &error) {
        return errorOf(error.GetCode());
    } catch (const std::exception &) {
        // Anything else, e.g. running out of memory on a huge or hostile file:
        // the port promises a result, not an exception.
        return InspectionError::Damaged;
    }
}

} // namespace misign::adapters
