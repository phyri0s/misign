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

## Dependencies

Only GPLv3-compatible licenses are accepted: MIT, BSD, Apache 2.0, LGPL, GPLv3.

## Architecture decisions

Every structural decision is recorded as an ADR in [docs/adr/](docs/adr/), based on the [template](docs/adr/0000-template.md).
