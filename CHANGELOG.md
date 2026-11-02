# Changelog

What changed for people who use this grammar, newest first. See
[ADR-0002](docs/adr/0002-changelog-is-for-users.md) for what belongs here.
Changes before the fork are in the upstream project's history.

## [Unreleased]

Changes since the upstream project's 0.2.0.

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
