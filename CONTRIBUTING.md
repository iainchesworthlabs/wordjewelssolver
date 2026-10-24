# Contributing

Thanks for taking a look at WordJewelsSolver. This guide is short: it is meant
to save you a round trip on the handful of things that are not obvious from the
code.

## Before you start

For anything more than a typo fix, it is worth opening an issue first to say
what you are thinking. It avoids duplicated effort and lets us agree on the
approach before you spend time on it. A security problem is the exception:
report it privately, as the [security policy](SECURITY.md) says, not in an
issue. Everyone taking part is held to the [code of conduct](CODE_OF_CONDUCT.md).

Please check [docs/roadmap.md](docs/roadmap.md) and
[docs/architecture.md](docs/architecture.md) for what is planned and what is
deliberately not built yet.

## Branching model

The project is trunk-based:

- **`develop`** is the trunk and the default branch. Target your pull request
  here. It is protected: nothing is pushed to it directly (see
  [docs/branch-protection.md](docs/branch-protection.md)).
- **`main`** holds published releases only. A release is a `v*` tag on a
  `develop` commit, and the release workflow moves `main` to it. Nobody opens a
  pull request into `main`.
- Work branches start with `feature/`, `bugfix/`, `docs/`, `chore/` or `agent/`,
  followed by a short name of lower-case letters, digits and `-`. A pull request
  from any other name is refused by the Policy check, and a `pre-push` hook in
  `tools/hooks` tells you sooner (see below).
- Pull requests are squash-merged through the merge queue, so each lands on
  `develop` as one commit.

## Set up your build

[docs/building.md](docs/building.md) has the requirements, the presets and the
options. The short version, on Windows:

```sh
git clone https://github.com/iainchesworthlabs/wordjewelssolver
cd wordjewelssolver
# VCPKG_ROOT must point at a clone of the labs' vcpkg fork (docs/building.md)
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

Use `linux-gcc-debug` or `macos-clang-debug` on those systems. All three are
built and tested by CI; a problem on a compiler or system it does not cover is a
useful report. Several of the repository's checks are PowerShell 7 scripts
(`pwsh`); the tests that need it skip with a message when it is not installed, and
CI runs them.

## Tests are not optional

New behaviour comes with a test, and a bug fix with the test that failed before
it. [docs/testing.md](docs/testing.md) says where each kind of test lives and
how to add one. Debug builds run under AddressSanitizer, and every first-party
target is compiled with warnings as errors, so a warning is a build failure:
fix it rather than silence it.

## Platform code: the one rule that is not about style

The operating system is chosen by CMake, not by the source. Code that differs
between systems lives in `libs/platforms/<name>/`, behind a header in
`libs/platforms/interfaces/`, and the platform's `platform.cmake` names the files
it compiles. Outside `libs/platforms/` and `tests/platform/` there is:

- no `#if`, `#ifdef`, `#else` or `#endif`, and no `#define` or `#undef`, in any
  first-party C++;
- no operating system header (`<windows.h>`, `<unistd.h>`, `<sys/...>`) and no
  call that asks which system this is (`QSysInfo`, `Q_OS_*`, `Qt.platform`);
- no `WIN32`, `APPLE` or `UNIX` test, and no `CMAKE_SYSTEM_NAME`, in any CMake
  file but `cmake/Platform.cmake` and the platform fragments.

`tools/checks/check_platform_isolation.ps1` enforces it, and it also runs as the
`platform.isolation` test, so you will see a violation locally:

```sh
pwsh ./tools/checks/check_platform_isolation.ps1
```

[docs/platform-isolation.md](docs/platform-isolation.md) explains the reasoning,
what to do instead of an `#ifdef`, and how to add a platform.

## Code style

- C++23, with compiler extensions off. The code uses `std::expected` for
  failures that are expected and does not throw for them; follow the style of
  the code around you.
- Formatting follows the repository's [`.clang-format`](.clang-format): run
  `clang-format` before committing, or have your editor do it on save.
- [`.clang-tidy`](.clang-tidy) is the static-analysis configuration. Warnings in
  the compiler's own set are errors in every build.
- Keep the library GUI-free and platform-free. Qt belongs in `apps/app` and
  nowhere else.

## Commits and pull requests

- Write each commit message as a short summary line in the imperative ("Add
  ...", "Fix ...") and, where the reason is not obvious, a few lines on why.
- Sign off every commit with `git commit -s` (see "Developer Certificate of
  Origin" below). The sign-off line is the only trailer a commit message has. A
  CI check reads every commit of a pull request (`docs/ci.md` describes it), and
  a `commit-msg` hook runs a similar script on your machine, if you want to be
  told before you push.
- To turn the hooks on (`commit-msg` for the commit text, `pre-push` for the
  branch name), run this once per clone. The `commit-msg` hook needs PowerShell
  7; without it, it says so and lets the commit through, and CI still checks:

  ```sh
  git config core.hooksPath tools/hooks
  ```

- Keep pull requests focused. A bug fix does not need an unrelated refactor
  riding along with it.
- If a person using the program would notice the change, add a fragment to
  [changelog.d](changelog.d/README.md). Do not edit [CHANGELOG.md](CHANGELOG.md)
  in a pull request.
- If you change what a requirement says, or rename or remove a test, correct its
  row in [docs/requirements/](docs/requirements.md): the `requirements.matrix`
  test names the rows that point at a test that is gone.
- Documentation is built strictly in CI (a broken link or heading is a red build).
  To check before you push: `pip install -r .github/requirements/docs.txt` and
  `mkdocs build --strict --config-file .github/mkdocs-ci.yml`.
- Do not commit a word list whose source and licence are not recorded in
  `data/README.md` (see
  [docs/licensing.md](docs/licensing.md#the-word-list)), a picture of a person,
  a trained model, or build output. The `.gitignore` already keeps camera
  captures, model files and build output out; do not work around it.

## Opening a pull request

1. Push your topic branch and open a pull request against `develop`.
2. Make sure the checks are green. [docs/ci.md](docs/ci.md) says what runs, and
   [docs/branch-protection.md](docs/branch-protection.md) how the protected
   branches are set up.
3. Describe what changed and why, and what you ran to check it, on which system.
4. Once the checks are green it is squash-merged through the merge queue.

## Licence of contributions

Contributions are accepted under the project's licence (see [LICENSE](LICENSE)
and [docs/licensing.md](docs/licensing.md)): source files are
GPL-3.0-or-later.

### Developer Certificate of Origin

By adding a `Signed-off-by` line to a commit you certify the following, from
[developercertificate.org](https://developercertificate.org/):

> (a) The contribution was created in whole or in part by me and I have the
> right to submit it under the open source license indicated in the file; or
>
> (b) The contribution is based upon previous work that, to the best of my
> knowledge, is covered under an appropriate open source license and I have the
> right under that license to submit that work with modifications, whether
> created in whole or in part by me, under the same open source license (unless
> I am permitted to submit under a different license), as indicated in the file;
> or
>
> (c) The contribution was provided directly to me by some other person who
> certified (a), (b) or (c) and I have not modified it.
>
> (d) I understand and agree that this project and the contribution are public
> and that a record of the contribution (including all personal information I
> submit with it, including my sign-off) is maintained indefinitely and may be
> redistributed consistent with this project or the open source license(s)
> involved.

That is all. If anything here is unclear or out of date, saying so in an issue
is itself a welcome contribution.
