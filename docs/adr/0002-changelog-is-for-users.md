# ADR-0002: The changelog tells users what changed; it is not a git log or a task list

**Status:** Accepted, 2026-11-02 (Brandon Arrendondo). Proposed 2026-11-02.

## Context

A user of this grammar, such as a formatter, an editor's highlighting or a
documentation scanner, needs to know what a release changes in the trees it
produces: which documents now parse, and which constructs now read
differently. A list of commits mixes those changes with implementation
history.

## Decision

`CHANGELOG.md` is a curated record for users of the grammar. Each release
has one dated section, newest first, with Unreleased on top. Write the
entry in the same change that ships the user-visible effect, under
Unreleased. Use these headings only when they have entries:

1. **Added:** a construct the grammar now parses, a new node or field, or a
   new binding or query.
2. **Fixed:** a document that failed to parse, or parsed into the wrong
   structure, where docutils reads it correctly. Describe the construct and
   the resulting tree in user terms.
3. **Removed:** a node, field, binding or query a user could have relied
   on.
4. **Changed:** a change to the shape of the tree for documents that
   already parsed (a node renamed, nested differently, or split), including
   one that corrects a bug. Users' queries and code that walks the tree may
   need updating, so say which constructs change.

Entries are short publication-ready explanations, not copied commit
subjects or work-item titles. Exclude tests, corpus work, docs-only edits,
CI and packaging chores, refactors and regenerated files with no
user-visible effect.

Do not publish internal tracking references. The grammar's defects,
including those inherited from the original project, are this project's
own: describe their fixes like any other. Do not locate an unfixed defect
in any other project.

## Consequences

- Release notes require editorial review; automation can collect
  candidates but cannot decide that every change deserves an entry.
- The first section starts from the upstream commit the fork is based on;
  earlier changes are in the upstream project's history.

Origin: adaptation of Moldy ADR-0002, itself from knots ADR-0004 and
aurora-lint ADR-0009, restated for a grammar's users.
