#pragma once

#include "application/pdf_inspector.h"

namespace misign::adapters {

// Reads documents with PoDoFo, through its raw getters only: the others adjust
// coordinates for the page rotation (ADR 0005).
class PodofoPdfInspector final : public application::IPdfInspector {
public:
    [[nodiscard]] application::InspectionResult
    inspect(const std::filesystem::path &file) const override;
};

} // namespace misign::adapters
