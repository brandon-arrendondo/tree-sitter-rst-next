"""
Invoke tasks for tree-sitter-rst-next development.

Usage:
    invoke generate       # Regenerate src/ from grammar.js
    invoke build          # Check src/ is current, then build the Rust binding
    invoke test           # Grammar corpus tests and Rust binding tests
    invoke check          # Run every pre-commit hook on all files
    invoke parse FILE     # Print the tree for one document
    invoke bump-version   # Set the version in every file that embeds it
    invoke clean          # Remove build artifacts

The tree-sitter CLI is the one named by TREE_SITTER (default: tree-sitter on
PATH). It must be the version in package.json's tree-sitter-cli; another
version generates a different src/parser.c.

Install invoke: pip install invoke
"""

import os
import re
from pathlib import Path

from invoke import Exit, task

ROOT = Path(__file__).resolve().parent
TREE_SITTER = os.environ.get("TREE_SITTER", "tree-sitter")

# A semver core like 0.1.0 (no pre-release / build metadata).
SEMVER = r"\d+\.\d+\.\d+"

# Files that embed the grammar's own version, with the pattern that locates
# it. The tool versions pinned in .pre-commit-config.yaml are not this
# grammar's version and are not touched.
VERSION_FILES = [
    ("Cargo.toml", r'^(version = ")' + SEMVER + r'(")'),
    ("package.json", r'^(  "version": ")' + SEMVER + r'(")'),
    ("tree-sitter.json", r'^(    "version": ")' + SEMVER + r'(")'),
    ("pyproject.toml", r'^(version = ")' + SEMVER + r'(")'),
    ("Makefile", r"^(VERSION := )" + SEMVER + r"()"),
    ("CMakeLists.txt", r'^(        VERSION ")' + SEMVER + r'(")'),
]


def _read_cargo_version():
    cargo = (ROOT / "Cargo.toml").read_text()
    match = re.search(r'^version = "([^"]+)"', cargo, re.MULTILINE)
    if not match:
        raise Exit("Could not find version in Cargo.toml", code=2)
    return match.group(1)


def _require_cli(c):
    """Fail unless the tree-sitter CLI matches package.json's pin."""
    pinned = re.search(
        r'"tree-sitter-cli": "\^?(' + SEMVER + r')"',
        (ROOT / "package.json").read_text(),
    )
    out = c.run(f"{TREE_SITTER} --version", hide=True, warn=True)
    found = out.stdout.split()[1:2] if out.ok else []
    if not pinned or found != [pinned.group(1)]:
        want = pinned.group(1) if pinned else "the package.json version"
        raise Exit(
            f"tree-sitter CLI {want} required (set TREE_SITTER), found: "
            f"{out.stdout.strip() or 'nothing'}",
            code=2,
        )


@task
def generate(c):
    """Regenerate src/ from grammar.js."""
    _require_cli(c)
    c.run(f"{TREE_SITTER} generate", pty=True)


@task
def build(c, release=False):
    """Check that src/ matches grammar.js, then build the Rust binding.

    Args:
        release: Build in release mode (default: debug).
    """
    generate(c)
    c.run("git diff --exit-code -- src/", pty=True)
    c.run("cargo build" + (" --release" if release else ""), pty=True)


@task
def test(c):
    """Run the grammar corpus tests and the Rust binding tests."""
    _require_cli(c)
    c.run(f"{TREE_SITTER} test", pty=True)
    c.run("cargo test", pty=True)


@task
def check(c):
    """Run every pre-commit hook on all files."""
    c.run("pre-commit run --all-files", pty=True)


@task
def parse(c, file):
    """Print the parse tree of one document."""
    _require_cli(c)
    c.run(f"{TREE_SITTER} parse {file}", pty=True, warn=True)


@task
def bump_version(c, new_version=None):
    """Set the grammar's version in every file that embeds it.

    Reads the current version from Cargo.toml. With no --new-version, prints
    the current version and the files that would change.

    Args:
        new_version: Target version string, e.g. 0.3.0 (no leading 'v').
    """
    current = _read_cargo_version()
    if not new_version:
        print(f"Current version (Cargo.toml): {current}")
        print("\nFiles that would be updated:")
        for path, _ in VERSION_FILES:
            print(f"  {path}")
        print("\nRun: invoke bump-version --new-version X.Y.Z")
        return
    if not re.fullmatch(SEMVER, new_version):
        raise Exit(f"--new-version must look like X.Y.Z, got '{new_version}'", code=2)

    changed = []
    for path, pattern in VERSION_FILES:
        p = ROOT / path
        text = p.read_text()
        updated = re.sub(
            pattern, rf"\g<1>{new_version}\g<2>", text, count=1, flags=re.MULTILINE
        )
        if updated == text:
            raise Exit(
                f"{path}: no version string matched; update VERSION_FILES", code=2
            )
        p.write_text(updated)
        changed.append(path)
    # The lock files record the package's own version too.
    c.run("cargo update --workspace --quiet", pty=True)
    c.run(
        "npm install --package-lock-only --ignore-scripts --silent", pty=True, warn=True
    )
    print(f"Bumped {current} -> {new_version} in:")
    for path in changed + ["Cargo.lock", "package-lock.json"]:
        print(f"  {path}")
    print("\nNext: update CHANGELOG.md, review `git diff`, and commit.")


@task
def clean(c):
    """Remove build artifacts."""
    c.run("cargo clean", pty=True)
    c.run("rm -rf build", pty=True)
