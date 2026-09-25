#!/usr/bin/env python3
"""Check commit messages: Conventional Commits subject and DCO sign-off.

Used as a pre-commit `commit-msg` hook, with the path of the message file:

    check_commit_message.py .git/COMMIT_EDITMSG

and in CI, on every non-merge commit of a pull request:

    check_commit_message.py --range origin/dev..HEAD

In range mode the sign-off must also match the commit author, like the DCO
GitHub app, and fixup!/squash!/amend! commits are rejected: they must be squashed
before the merge. Merge commits need no sign-off, and commits authored by bots
(Dependabot) are exempt from the sign-off and the subject length.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

TYPES = ("build", "chore", "ci", "docs", "feat", "fix", "perf", "refactor", "revert", "style", "test")
SUBJECT = re.compile(rf"^(?:{'|'.join(TYPES)})(?:\([a-z0-9._/-]+\))?!?: \S")
# Messages written by git itself (merges also run the commit-msg hook), and the
# autosquash prefixes, allowed locally only.
MERGE = re.compile(r"^Merge ")
GIT_GENERATED = re.compile(r'^(?:Merge |Revert ")')
AUTOSQUASH = re.compile(r"^(?:fixup|squash|amend)! ")
SIGN_OFF = re.compile(r"^Signed-off-by: .+ <(?P<email>[^>]+)>$", re.MULTILINE)
MAX_SUBJECT_LENGTH = 100
SCISSORS = "# ------------------------ >8 ------------------------"


def clean(message: str) -> str:
    """Drop what git strips from the final message: the diff below the scissors line
    (`git commit -v`) and comment lines."""
    message = message.split(SCISSORS, 1)[0]
    return "\n".join(line for line in message.splitlines() if not line.startswith("#")).strip()


def check(message: str, author_email: str | None = None, is_bot: bool = False,
          allow_autosquash: bool = True) -> list[str]:
    message = clean(message)
    if not message:
        return ["the commit message is empty"]
    subject = message.splitlines()[0]
    errors = []

    if AUTOSQUASH.match(subject):
        if not allow_autosquash:
            errors.append(f"squash this commit into its target before merging: {subject!r}")
    elif not GIT_GENERATED.match(subject):
        if not SUBJECT.match(subject):
            errors.append(
                f"the subject does not follow Conventional Commits: {subject!r}\n"
                f"    expected '<type>[(scope)][!]: <description>', with type one of: {', '.join(TYPES)}"
            )
        if len(subject) > MAX_SUBJECT_LENGTH and not is_bot:
            errors.append(f"the subject is {len(subject)} characters long (at most {MAX_SUBJECT_LENGTH})")

    if not is_bot and not MERGE.match(subject):
        emails = [m.group("email") for m in SIGN_OFF.finditer(message)]
        if not emails:
            errors.append("the 'Signed-off-by:' line is missing: commit with 'git commit -s' (DCO)")
        elif author_email is not None and author_email not in emails:
            errors.append(f"no 'Signed-off-by:' line matches the author <{author_email}>")
    return errors


def git(*args: str) -> str:
    return subprocess.run(["git", *args], check=True, capture_output=True, text=True).stdout


def check_range(revision_range: str) -> int:
    failures = 0
    for sha in git("rev-list", "--no-merges", "--reverse", revision_range).split():
        author_name, author_email = git("show", "-s", "--format=%an%n%ae", sha).splitlines()
        is_bot = author_name.endswith("[bot]")
        errors = check(git("show", "-s", "--format=%B", sha), author_email, is_bot, allow_autosquash=False)
        if errors:
            failures += 1
            print(f"{sha[:10]}: {git('show', '-s', '--format=%s', sha).strip()}")
            for error in errors:
                print(f"  - {error}")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("message_file", nargs="?", type=Path, help="commit message file (commit-msg hook)")
    group.add_argument("--range", dest="revision_range", help="check every non-merge commit of this range")
    args = parser.parse_args()

    if args.revision_range:
        return check_range(args.revision_range)

    errors = check(args.message_file.read_text(encoding="utf-8"))
    for error in errors:
        print(f"commit message: {error}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
