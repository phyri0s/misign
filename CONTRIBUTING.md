# Contributing to Misign

All project content is written in English: documentation, code comments, commit messages, pull requests.

## Branches

- `main` → production: only receives merges from `dev`. Each merge publishes a stable release (`v1.2.0`).
- `dev` → development: integration branch. Each merge publishes a pre-release (`v1.3.0-dev.4`).
- Work branches: created from `dev`, named after the Linear issue, then merged into `dev` through a pull request.
  - `feature/PHY-12-signature-box`
  - `fix/PHY-20-page-rotation`

`main` and `dev` are protected: no direct pushes, merges only when CI is green. Every change goes through a pull request, even when working solo.

## Commits

Commit messages follow [Conventional Commits](https://www.conventionalcommits.org/): `feat:`, `fix:`, `docs:`, `test:`, `refactor:`, `chore:`, `ci:`… The changelog and version numbers are generated from them.

## DCO

Every commit must be signed off under the [Developer Certificate of Origin](https://developercertificate.org/):

```sh
git commit -s -m "feat: ..."
```

This adds a `Signed-off-by: Name <email>` line certifying that you have the right to submit the code under GPLv3.

## Local checks

[pre-commit](https://pre-commit.com/) runs the same checks as the "Format" CI job before each commit: C++ formatting (clang-format 18.1.3), QML formatting (`qmlformat`), file hygiene, and the commit message (Conventional Commits and DCO sign-off). See [ADR 0007](docs/adr/0007-code-style-and-local-checks.md).

The dev container ships pre-commit and Qt's tools. Install the hooks once per clone, from the environment where you commit:

```sh
pre-commit install                # pre-commit and commit-msg hooks
pre-commit run --all-files        # check the whole repository
```

On the host, install pre-commit with `pipx install pre-commit` (or `pip install --user pre-commit`) and put Qt's `bin` directory on `PATH` for `qmlformat`. Formatters fix the files in place: review the changes, stage them and commit again.

Static analysis needs a build tree, so it runs in CI rather than in the hooks. To run it locally, after a build:

```sh
# C++: the tracked sources only, as in CI (build/debug instead of build-container/debug on the host)
git ls-files 'src/*.cpp' 'tests/*.cpp' | xargs run-clang-tidy-18 -p build-container/debug -quiet
# QML
cmake --build --preset debug --target all_qmllint
```

## Dependencies

Only GPLv3-compatible licenses are accepted: MIT, BSD, Apache 2.0, LGPL, GPLv3.

## Architecture decisions

Every structural decision is recorded as an ADR in [docs/adr/](docs/adr/), based on the [template](docs/adr/0000-template.md).
