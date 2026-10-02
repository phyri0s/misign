# 0011 — No saved signatures

- **Status**: accepted
- **Date**: 2026-10-02

## Context

PHY-105 planned to let the user draw a signature once, keep it, and apply it to other documents in later sessions, through an `ISignatureStore` port listed in ADR 0001. The storage side was implemented (a JSON file per signature in the user's application data directory, pull request #41) while the question of how to protect the files stayed open.

A saved signature is a reusable image of the user's handwritten signature. Anyone who can use the account, or any program running as the user, can apply it to a document. An OS keychain does not change that: DPAPI and the Secret Service decrypt for any program of the logged-in user, so encryption would only protect the files at rest, which full-disk encryption already does.

Beyond that risk, a handwritten signature is worth something because it is drawn by the signer for the document in front of them. A stored drawing pasted onto a document is a stamp, not a signature.

## Options considered

- **Plain files, readable by their owner only**: no dependency, but anyone who can use the account can reuse the signature.
- **Files encrypted with a key from the OS keychain**: protects the files at rest only, and adds a dependency, a service that may be missing on Linux, and a key that can be lost.
- **A password asked by Misign when a saved signature is used**: protects against someone else at the keyboard, not against a program running as the user.
- **No saved signatures**: the signature is drawn again for every document. Nothing to protect, and each signature is an act of the signer.

## Decision

No saved signatures. Misign does not store drawn signatures: a signature exists in memory while a document is being signed and is written only into the signed copy of that document. Every signature is drawn by hand each time, which is what gives it its value.

The `ISignatureStore` port, the `JsonSignatureStore` adapter and their tests are removed, and PHY-105 is cancelled.

## Consequences

- Misign keeps no signature data on disk, so there is nothing to protect, document or migrate.
- Signing several documents means drawing the signature for each of them. Placing several signatures in one document (PHY-100) is a separate question, inside a single signing session.
- The drawing pad has no "save" action and no list of saved signatures.
