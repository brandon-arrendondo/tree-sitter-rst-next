# Changelog

What changed for people who use this grammar, newest first. See
[ADR-0002](docs/adr/0002-changelog-is-for-users.md) for what belongs here.
Changes before the fork are in the upstream project's history.

## [Unreleased]

### Fixed

- A bullet or enumerator alone on its line, with its body on the lines
  below, takes the body's indent from its least indented line, as
  docutils does. A body indented less than a wide enumerator (`(10)`
  over three spaces) was left out of the item, and a body indented past
  the marker was read as a block quote inside it.
- The Rust crate rebuilds when a scanner source under
  `src/tree_sitter_rst/` changes. Only `src/parser.c` and `src/scanner.c`
  were watched, so a build from a local checkout or a path dependency
  kept the old scanner after an edit to the files `scanner.c` includes.
- A comment whose text starts on the line below a bare `..` marker takes
  its body's indent from the least indented line, as docutils does. A
  later line indented less than the first no longer ends the comment and
  leaves a syntax error.
- A paragraph that starts with an enumerator (`1.`) and continues on an
  unindented line is a paragraph, as in docutils, also when that line
  starts with a word and a period (`assignments.`) or with punctuation
  that doesn't underline it (``` ``literal`` ```, `` `link`_ ``). The word
  was taken for the next enumerator, and the punctuation for a title
  underline, and the paragraph was read as a list.
- A list, definition list, block quote, directive or comment followed
  directly by a less indented line, with no blank line between, ends
  there, and the line starts the next element, as in docutils (which
  warns about the unexpected unindent). It was a syntax error.

## [0.3.0] - 2026-11-02

The first release of tree-sitter-rst-next. Changes since the fork from the
upstream project at commit `a60f107` (0.2.0 and the changes after it). Of
those later upstream changes, two reach users of this release: the scanner saves and restores its
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

- A directive's arguments run, as in docutils, from its first line to the
  first blank line or option (`.. function:: f(a,` with `b)` on the next
  line is one `arguments` node). Only the first line was read as
  arguments, and the lines after it, the blank line and the body became
  one `content` node.
- The body of a directive, comment or other explicit markup is indented as
  far as its least indented line, as in docutils. A first line indented
  deeper than the lines after it ended the body at the next line.

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

[Unreleased]: https://github.com/brandon-arrendondo/tree-sitter-rst-next/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/brandon-arrendondo/tree-sitter-rst-next/releases/tag/v0.3.0
