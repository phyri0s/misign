#pragma once

#include "application/signature_store.h"

#include <QString>

namespace misign::adapters {

// Keeps each saved signature in its own JSON file, `<id>.json`, in one
// directory (ADR 0011). The files and the directory are readable by their
// owner only where the file system supports it; they are not encrypted.
//
// Format, version 1:
//
//     {
//       "format": "misign-signature",
//       "version": 1,
//       "name": "Signature",
//       "savedAt": "2026-09-27T17:40:00.123Z",
//       "strokes": [
//         {"inputMode": "stylus", "points": [[x, y, pressure, timestamp], ...]}
//       ]
//     }
//
// Points are as in domain::Point: drawing-surface units, pressure in [0, 1],
// timestamp in whole microseconds. inputMode is "stylus", "touchpad" or
// "mouse". A file with a higher version, or not matching this format, is not
// listed and is left untouched.
class JsonSignatureStore final : public application::ISignatureStore {
public:
    // `directory` is created on the first save.
    explicit JsonSignatureStore(QString directory);

    // The "signatures" directory in the user's application data directory
    // (QStandardPaths::AppDataLocation). Needs the application and
    // organization names to be set first.
    [[nodiscard]] static QString defaultDirectory();

    [[nodiscard]] std::vector<application::SavedSignature> list() const override;
    std::optional<application::SavedSignature> save(const std::string &name,
                                                    const domain::Signature &signature) override;
    bool rename(const std::string &id, const std::string &name) override;
    bool remove(const std::string &id) override;

private:
    // The file of `id`, none when `id` is not one this store could have
    // chosen, so an id can never point outside the directory.
    [[nodiscard]] std::optional<QString> pathOf(const std::string &id) const;

    QString m_directory;
};

} // namespace misign::adapters
