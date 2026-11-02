# tree-sitter-rst-next

A reStructuredText grammar that reads documents the way docutils does, so
that tools built on it can trust the tree on real documentation. It started
as a fork of [stsewd/tree-sitter-rst](https://github.com/stsewd/tree-sitter-rst)
by Santos Gallegos and is maintained here as its own project. Changes since
the fork are listed in
[CHANGELOG.md](CHANGELOG.md); contributors start with [AGENTS.md](AGENTS.md)
and [CLAUDE.md](CLAUDE.md).

reStructuredText grammar for [tree-sitter](https://tree-sitter.github.io/tree-sitter/).
Based on the specification from  <https://docutils.sourceforge.io/docs/ref/rst/restructuredtext.html>.

Try it in the playground: `make serve`, then open the page it prints.

**Note: this grammar is still under development, a lot of things may change!**

## TODO

- Allow lists with blank lines between items
- Refactor parse citation and footer reference
- Nested line blocks
- Add some nodes to inline?
- Check if there is a way to re-implement some nodes to JS instead of C?
- tests, tests, and more tests!

## Design notes

- Nodes that contain body elements inside (like lists or directives),
  are inside a node named `body`.
- In RST sections are a big node that contains body elements,
  here they are just a node containing the title.
  This is to avoid guessing the level of subtitles.
- In RST standalone hyperlinks are `reference` nodes,
  here they are `standalone_hyperlink` nodes.
  This is to avoid confusion with inline references nodes
  (that are a `reference` node).
- Literal blocks are part of the paragraph they precede, not a separate node,
  except for the expanded form of literal blocks.
- In rst directives only have "body" node,
  here the body is parsed as arguments/options/content.

## Design decisions

- Implement tables?
- Validate length of adornments in sections?

  The spec doesn't mention this, but this paragraph does.

  > The underline/overline must be at least as long as the title text.

  https://docutils.sourceforge.io/docs/user/rst/quickstart.html


## Contributing

Check the [CONTRIBUTING.md](CONTRIBUTING.md) file for how the grammar and
scanner work, and [AGENTS.md](AGENTS.md) for the commit rules (DCO sign-off,
hooks). Run `invoke check` and `invoke test` before committing.

## AI Assistance

This project's changes since the fork were developed with assistance from
[Claude](https://claude.ai) (Anthropic), used for the grammar and scanner
fixes, their tests, and the repository's tooling. Each change was reviewed
before it was merged. The contribution is acknowledged once, here, and not
with a co-author trailer on each commit. Brandon Arrendondo is the
accountable reviewer.

## License

MIT. Copyright 2026 BISSELL Homecare, Inc. The original grammar is
Copyright 2020 Santos Gallegos, credited under the MIT License. See
[LICENSE](LICENSE).
