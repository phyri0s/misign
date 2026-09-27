# 0011 — Saved signatures

- **Status**: proposed
- **Date**: 2026-09-27

## Context

A signature drawn once can be kept and applied to other documents in later sessions (PHY-105, the `ISignatureStore` port of ADR 0001). It must stay vector data, so it can be fitted into any box (`Signature::fitInto`), and stay local: no sync, no network.

A saved signature is a reusable image of the user's handwritten signature. Anyone who can use the account, or any program running as the user, can apply it to a document. The ticket asks whether it should be protected, for example encrypted with the OS keychain, and at least that the risk be documented.

What a keychain would protect against, read in the documentation and not tried:

- On Windows, DPAPI and the Credential Manager decrypt for any program running as the logged-in user. On Linux, the Secret Service (GNOME Keyring, KWallet) is unlocked at login and then serves any program of the session. So a keychain does not protect against the account, which is the risk the ticket names.
- It protects the file at rest: a disk read from another system, a backup of the application data, another local account if the file permissions were wrong. Full-disk encryption (BitLocker, LUKS) already covers the first two, for every file of the user.
- A drawn signature is not a secret the way a password is: it appears on every document the user signs, and anyone who received one can copy it from the PDF.
- It costs a dependency (QtKeychain, BSD-3-Clause), a Secret Service that may be missing on Linux (a minimal window manager, a remote session), and a key that can be lost, taking the saved signatures with it. The Credential Manager also caps a secret at 2,560 bytes, so the files would be encrypted with a key kept there rather than stored in it.

## Options considered

- **Plain JSON files, readable by their owner only**: no dependency, readable and portable, a format that can be versioned and checked. Anyone who can use the account can read and reuse them.
- **Files encrypted with a key from the OS keychain**: protects the files at rest where the disk is not encrypted, not against the account. Adds a dependency, a service that may be missing, and a way to lose every saved signature.
- **A password asked by Misign when a saved signature is used**: protects against someone else at the keyboard, not against malware running as the user, which can wait for the password. One more password for a signature anyone can copy from a signed PDF.
- **No saved signatures**: no risk, but the signature must be drawn again for every document, which is what the ticket is for.

## Decision

Plain JSON files, one per signature, in the "signatures" directory of the user's application data directory (`QStandardPaths::AppDataLocation`, e.g. `~/.local/share/Misign/Misign/signatures` on Linux, `%APPDATA%/Misign/Misign/signatures` on Windows). The directory and the files are created readable by their owner only (0700 and 0600 on Linux); on Windows the application data directory is already private to the account. The files are not encrypted.

The format is versioned, documented in `src/adapters/json_signature_store.h` and pinned by `tests/integration/tst_json_signature_store.cpp`: points keep their exact coordinates, pressure, timestamp and input mode, so a saved signature is the drawing itself. A file that cannot be read, damaged or written by a newer Misign, is not listed and never overwritten.

The UI says, where a signature is saved, that anyone who can use this account can use the saved signature, and that deleting it removes its file.

## Consequences

- Saving a signature has the same exposure as leaving a scan of it in the user's documents, and the UI says so.
- No dependency, and nothing to lose besides the files, which can be copied to another machine.
- If a threat that encryption would address comes up, a version 2 of the format can add it; version 1 files stay readable.
