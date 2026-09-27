#pragma once

#include "domain/signature.h"

#include <optional>
#include <string>
#include <vector>

namespace misign::application {

// A signature kept for reuse, under the name the user gave it.
struct SavedSignature {
    // Chosen by the store when saving; stays the same when renamed.
    std::string id;
    // UTF-8, as the user typed it.
    std::string name;
    domain::Signature signature;

    friend bool operator==(const SavedSignature &, const SavedSignature &) = default;
};

// Where drawn signatures are kept, so one drawing can be applied to other
// documents in later sessions, fitted into any box (Signature::fitInto). They
// stay vector data. Local only: no sync, no network (ADR 0011).
class ISignatureStore {
public:
    ISignatureStore() = default;
    ISignatureStore(const ISignatureStore &) = delete;
    ISignatureStore &operator=(const ISignatureStore &) = delete;
    ISignatureStore(ISignatureStore &&) = delete;
    ISignatureStore &operator=(ISignatureStore &&) = delete;
    virtual ~ISignatureStore() = default;

    // Every saved signature, most recently saved first. An entry that cannot be
    // read, damaged or written by a newer Misign, is left out.
    [[nodiscard]] virtual std::vector<SavedSignature> list() const = 0;

    // Saves `signature` under `name` and returns the new entry. None when it
    // could not be written, or when the signature is empty.
    virtual std::optional<SavedSignature> save(const std::string &name,
                                               const domain::Signature &signature) = 0;

    // Whether the entry was found and its new name written.
    virtual bool rename(const std::string &id, const std::string &name) = 0;

    // Whether the entry was found and deleted, along with its data.
    virtual bool remove(const std::string &id) = 0;
};

} // namespace misign::application
