# Changelog

What changed for people who use this grammar, newest first. See
[ADR-0002](docs/adr/0002-changelog-is-for-users.md) for what belongs here.
Changes before the fork are in the upstream project's history.

## [Unreleased]

Changes since the fork from the upstream project at commit `a60f107`
(0.2.0 and the changes after it). Of those later upstream changes, two
reach users of this release: the scanner saves and restores its
indentation state correctly, so re-parsing a document after an edit no
longer loses it, and the grammar is generated with tree-sitter 0.26.9.

### Changed

- The packages are named `tree-sitter-rst-next` (crates.io, npm and PyPI).
  The names code imports are unchanged: the language is `rst`, the Rust
  crate and Python module are `tree_sitter_rst`, and the C function is
  `tree_sitter_rst()`, so switching needs no code change. The package
  replaces the original `tree-sitter-rst` and cannot be installed beside
  it, because both provide the same module, library and C symbol:
  uninstall `tree-sitter-rst` first.
- The parser uses tree-sitter's language ABI 15 (it was 14) and needs a
  tree-sitter runtime of version 0.25 or later. The Python package's
  `core` extra and the npm package's `tree-sitter` peer dependency require
  0.25; the Rust crate depends only on `tree-sitter-language`, so a Rust
  project must use the `tree-sitter` crate 0.25 or later itself.

### Added

- Option lists are parsed (`-a`, `--long`, `--input=file`, `/V`, and
  groups such as `-f, --force`), with a description on the same line after
  two or more spaces or on the indented lines below. An argument may follow
  `=` or a single space, as a word (`-i FILE`) or in angle brackets
  (`-l <loops>`). An option with no description is text, as in docutils.

### Fixed

- Two or more spaces after a footnote or citation label (`.. [1]  Body`)
  opened a block quote inside the body; whitespace in the middle of a line
  is no longer read as an indent.
- A spaced colon (` : `) anywhere other than a definition list term, such
  as `Foo : bar` in a paragraph or list item, was a syntax error. It is now
  a classifier only on a term line followed directly by its definition. A
  term with a classifier no longer takes a block quote after a blank line
  as its definition.
- A `::` that opens no literal block (`("::")`, `Use this::` as a term, or
  `Para::` at the end of a paragraph with nothing indented after it) is
  text.
- A bullet list or option list starting with `-` at the start of a block
  quote was read as a paragraph.
- An enumerator followed by a line at the same indent that does not start
  another enumerator (`1. Start the thread` with its text continued on the
  next line, unindented) is paragraph text, as in docutils, instead of a
  list item that failed at the second line.
