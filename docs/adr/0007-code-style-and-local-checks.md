# 0007 — Code style and local checks

- **Status**: accepted
- **Date**: 2026-09-25

## Context

The code style must stay consistent, and mistakes (formatting, non-conventional commit messages, missing DCO sign-off) should be caught before a pull request, not by CI. Local checks and CI must agree: a commit accepted locally must not be rejected by CI for style, and the other way around.

Contributors work in the dev container or directly on Windows or Linux, and commit from either.

## Options considered

- **Hook framework**
  - [pre-commit](https://pre-commit.com/) (MIT): pins each hook's version in one file, installs the hooks in isolated environments, runs on Windows and Linux, and CI can run exactly the same hooks with `pre-commit run --all-files`. Needs Python.
  - Hand-written Git hooks in the repository (`core.hooksPath`): no dependency, but every tool version is whatever each machine has installed, and shell scripts do not run the same way on Windows.
- **C++ style**
  - Close to the Qt coding style, which the existing code already follows (4-space indent, function braces on their own line, `Type &name`): no churn, consistent with the Qt API the code calls.
  - A stock style (LLVM, Google, Chromium): no configuration to maintain, but reformats the existing code and diverges from Qt's own conventions.
- **Commit messages**
  - A small Python script in `scripts/`: one place for both rules (Conventional Commits and DCO), usable as a `commit-msg` hook and in CI on a commit range.
  - commitlint (Node.js) plus the DCO GitHub app: two tools, and a Node.js toolchain only for this.

## Decision

- **pre-commit** runs the local checks, installed with `pre-commit install` (`pre-commit` and `commit-msg` hooks). The "Format" CI job runs the same hooks on every file.
- **C++ formatting**: `.clang-format`, close to the Qt coding style, with a 100-column limit. clang-format is pinned to 18.1.3 through the `mirrors-clang-format` hook, the version the dev container ships.
- **C++ static analysis**: `.clang-tidy` enables the `bugprone`, `cert`, `clang-analyzer`, `concurrency`, `cppcoreguidelines`, `misc`, `modernize`, `performance`, `portability` and `readability` checks, minus a few noisy ones, with every finding treated as an error. `tests/.clang-tidy` relaxes the rules that clash with Qt Test conventions. clang-tidy needs a build tree, so it runs in CI (and on demand locally), not in the commit hooks.
- **QML**: `qmlformat` as a pre-commit hook; `qmllint` through the CMake `all_qmllint` target in CI, with `.qmllint.ini` turning every warning into a failure.
- **Commit messages**: `scripts/check_commit_message.py` checks the Conventional Commits subject and the `Signed-off-by:` line, as a `commit-msg` hook and in CI on the pull request commits. Merge commits need no sign-off (`git merge` runs the hook too, and CI skips merges). In CI the sign-off must also match the author, and `fixup!`/`squash!`/`amend!` commits, accepted locally, are rejected so they never reach `dev`. Bot commits (Dependabot) are exempt from the sign-off and the subject length.

## Consequences

- Contributors need Python and pre-commit where they commit, and Qt's `bin` directory on `PATH` for `qmlformat`. The dev container ships all of them.
- The hooks are not installed automatically in the dev container: the hook script records the path of the `pre-commit` executable, which would break commits made from the host on the same clone.
- Hook versions are bumped by hand with `pre-commit autoupdate`. The clang-format version should follow the one Ubuntu ships in the dev container, so that editor formatting matches the hook.
- Dependabot commit messages are configured to follow Conventional Commits.
