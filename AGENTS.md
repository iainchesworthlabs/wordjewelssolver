# Instructions for coding agents

This file applies to any automated contributor. Read it before changing the repository. People contributing by
hand will also find it a short list of what the checks expect; [CONTRIBUTING.md](CONTRIBUTING.md) has the rest.

## Working rules

- Work on a branch named `<prefix>/<slug>` with the prefix `feature`, `bugfix`, `docs`, `chore` or `agent`, and a
  lower-case slug with hyphens. Never commit to `develop` or `main` directly.
- Commit with `git commit -s`. Write the subject in the imperative, 72 characters or fewer, and the body as a plain
  description of what changed and why.
- Open pull requests against `develop`. Fill in the pull request template. Squash merge through the merge queue is the
  only method.
- Build and test with the presets in `CMakePresets.json` (`linux-gcc-debug`, `windows-msvc-debug`,
  `macos-clang-debug`). A change to code runs the preset for the machine at hand, and its tests, before the pull
  request.
- Do not create or edit secrets, tags, releases, repository settings or rulesets. The owner does those.
- Do not change the rules of the game, the solver's behaviour, the word lists, a file format or saved data without
  the owner's say. Tests that pin them (`docs/requirements.md`) are not edited to make a change pass.

## Code rules

- C++23, extensions off. The standard for all projects is
  [`docs/standard.md`](https://github.com/iainchesworthlabs/icl-common-infra/blob/develop/docs/standard.md) in
  `icl-common-infra`. This repository keeps the conventions it already had, which the standard allows (D9):
  - Format: the repository's `.clang-format` (Google-based, attached braces, 100 columns). Run `clang-format` on
    changed files; `clang-format` is a required check.
  - Names: types and enums `PascalCase`; functions, variables, constants and enumerators `lower_snake_case`;
    private data members end in an underscore (`rows_`); namespaces `lower_case` under `wjs`. The properties,
    signals and slots that QML sees are `camelCase`.
  - Return `std::expected` for failures that are expected; do not throw for them.
- No preprocessor directive outside `libs/platforms/` and `tests/platform/`: no `#if`, `#ifdef`, `#define`. The
  operating system is chosen by CMake (`docs/platform-isolation.md`).
- Qt belongs in `apps/app/src` and `apps/app/qml`. `libs/core` has no GUI and no platform code.
- New behaviour comes with a test. Tests sit next to the code they test (`docs/testing.md`).

## If you change X, update Y

| You change | Also update |
|---|---|
| A behaviour a person using the program would notice | A fragment in `changelog.d/` |
| What a requirement says, or the name of a test | Its row in `docs/requirements/` |
| A public header in `libs/*/include` | The tests, and `docs/architecture.md` when the layout changes |
| A dependency in `vcpkg.json` | Nothing else; Dependabot and the baseline check cover the fork |
| A CMake option or preset | `docs/building.md` |
| Test labels or phases | `docs/testing.md` |
| A workflow in `.github/workflows` | `docs/ci.md`; pin every `uses:` to a full SHA with a version comment |
| A string the user sees | `tr` or `qsTr`, then refresh the catalogues in `apps/app/translations` |
| A file under `data/` | `data/README.md`: source and licence of the list |
