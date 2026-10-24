# Changelog fragments

A pull request that changes something a person using the program would notice adds one file here, named
`<slug>.<type>.md`, where `<type>` is `added`, `changed`, `deprecated`, `removed`, `fixed` or `security`. The content
is one paragraph that starts with a bold summary:

```markdown
**The board accepts a three-letter tile.** A tile such as `Qu` or `Th` is read as one tile.
```

Do not edit `CHANGELOG.md` in a pull request. A pull request into `develop` assembles the fragments into a new
section before a release is tagged (`changelog_tool.py assemble` in `icl-common-infra`). A pull request that
changes only documentation, tests or tooling needs no fragment, and the `no-changelog` label covers the rest.
