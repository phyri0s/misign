#include "adapters/podofo_pdf_inspector.h"

#include <cstdint>
#include <fstream>
#include <podofo/podofo.h>
#include <string>

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

// PoDoFo's GetCropBoxRaw gives the MediaBox when there is no CropBox, so
// whether one is set, on the page or in the page tree, is read here.
bool hasCropBox(const PoDoFo::PdfPage &page)
{
    const PoDoFo::PdfObject *box = page.GetDictionary().FindKeyParent("CropBox");
    return box != nullptr && box->IsArray();
}

bool isSignatureWidget(const PoDoFo::PdfAnnotation &annotation)
{
    if (annotation.GetType() != PoDoFo::PdfAnnotationType::Widget) {
        return false;
    }
    // The field type may sit on a parent field, which FindKeyParent follows.
    const PoDoFo::PdfObject *type = annotation.GetDictionary().FindKeyParent("FT");
    const PoDoFo::PdfName *name = nullptr;
    return type != nullptr && type->TryGetName(name) && *name == "Sig";
}

PageInfo readPage(const PoDoFo::PdfPage &page)
{
    PageInfo info;
    info.mediaBox = asWritten(page.GetMediaBoxRaw());
    if (hasCropBox(page)) {
        info.cropBox = asWritten(page.GetCropBoxRaw());
    }
    double rotate = 0.0;
    page.TryGetRotationRaw(rotate);
    info.rotate = static_cast<int>(rotate);

    const PoDoFo::PdfAnnotationCollection &annotations = page.GetAnnotations();
    for (unsigned index = 0; index < annotations.GetCount(); ++index) {
        const PoDoFo::PdfAnnotation &annotation = annotations.GetAnnotAt(index);
        if (isSignatureWidget(annotation)) {
            const PoDoFo::Corners rect = annotation.GetRectRaw();
            info.signatureWidgets.push_back(
                PdfRect::fromCorners({rect.X1, rect.Y1}, {rect.X2, rect.Y2}));
        }
    }
    return info;
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
    }
}

} // namespace misign::adapters
