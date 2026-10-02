#pragma once

#include <cstdint>

namespace misign::application {

// Why a document could not be opened.
enum class DocumentError : std::uint8_t {
    // The file does not exist or cannot be read.
    Unreadable,
    NotAPdf,
    // A PDF whose structure cannot be read, e.g. a truncated file.
    Damaged,
    PasswordProtected,
};

} // namespace misign::application
