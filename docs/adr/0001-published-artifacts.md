# ADR-0001: A published artifact cites its sources, ships as a sanitized export, names its accountable reviewer, and discloses AI use

**Status:** Proposed, 2026-11-02. Awaits the maintainer's review. The
repository is private until its first release; this record must be
accepted, and met at the repository's head, before it is made public.

## Context

This repository started as a fork of an MIT-licensed grammar and will be
published as our own project: the repository, and packages built from it. Our other
public tools (aurora-lint, knots, Moldy) each hold the same four conditions
for anything they publish. Making a private repository public is the point
at which it must adopt them.

A fork adds a question those tools do not have: much of the code was
written by someone else, under their copyright, and our changes sit on top
of it.

## Decision

Before an artifact is submitted or published, it must meet four conditions.
An artifact is anything that leaves BISSELL: the public repository, a
package published to crates.io, npm, PyPI or another registry, or a release
archive.

1. **It cites sources correctly and is free of plagiarism.**
   - Verify every citation against the primary source. Do not write a
     citation from memory or a model's output.
   - Learn from sources without copying material their licences do not
     allow. Identify quotations as quotations and cite them. This applies
     to text, figures and code, including docutils' parser, which the
     grammar follows but does not copy.
   - The original author's MIT notice stays in `LICENSE`, and the README
     names the original project and its author. The history records what
     changed, who changed it and when.
2. **It is released only as a sanitized export.**
   - Publish only what the recipient needs: the grammar, its generated
     parser, bindings and documentation. Do not publish working records,
     corpus results or incidental build artifacts.
   - Export no personal information or intellectual property beyond what is
     intended for publication. Internal machine names and hostnames, IP
     addresses, local filesystem paths, tracking references and internal
     URLs do not ship.
   - The exception is authorship already made public by Git: commit author
     names and email addresses, including Brandon Arrendondo. Name any other
     BISSELL Associate only with permission obtained before submission.
3. **It names the accountable BISSELL Associate.** Record with the artifact
   who holds final review responsibility. That is Brandon Arrendondo unless
   the artifact explicitly names someone else.
4. **It discloses AI use in the form appropriate to the artifact.** Use the
   README's AI Assistance section. Commit trailers are not the disclosure
   mechanism. Contributor attribution rules remain in AGENTS.md.

Every artifact goes through **BISSELL legal review before submission or
publication**. **BISSELL Homecare, Inc. is the primary copyright
holder**, and the project is licensed under **MIT**, as recorded in
`LICENSE`. The original grammar's author, Santos Gallegos, is credited as
the MIT License requires: their copyright notice stays in `LICENSE` and the
README names them (ruled 2026-11-02, Brandon Arrendondo). Contributions require the DCO sign-off recorded in
AGENTS.md; a sign-off does not substitute for the accountable Associate's
artifact review.

These conditions carry out the BISSELL AI Acceptable Use Policy's
principles of accountability, data protection, intellectual property, data
quality and AI attribution.

## Consequences

- Package names are part of the review: upstream publishes
  `tree-sitter-rst` to crates.io, npm and PyPI, so this project's packages
  need names of their own, and its package metadata (authors, repository,
  funding links) must describe this project, not upstream's.
- Upstream's publishing workflow and funding link are not this project's
  and are removed; a release workflow is added when the first release is
  prepared.
- This is policy, not the export procedure. This ADR records no artifact as
  having completed legal review; automation cannot satisfy it.
- Fix a leak found in published text at the head. Whether to rewrite Git
  history is a separate maintainer decision; this ADR grants no blanket
  history-rewrite authorization.

Origin: adaptation of Moldy ADR-0001, itself adapted from knots ADR-0006 and
aurora-lint ADR-0016, with the fork's licensing and naming added.
