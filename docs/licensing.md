# Licensing

An engineering inventory of what the project is licensed under, what it uses,
and what is still open. It is not legal advice, and it does not change the
licence.

How to read the evidence marks:

- **[V]** verified against a file in this repository, an installed package or
  the git history.
- **[I]** inferred, or taken from the sibling project Cubeulator's licensing
  paper, which checked Qt 6.12's licences against Qt's own SBOMs. Treat as a
  lead, and re-check before relying on it.
- **[L]** a lawyer should confirm it before anyone relies on it.

## Summary

1. **The project is GPL-3.0-or-later** [V: `LICENSE`, `vcpkg.json`]. Each source
   file carries an SPDX identifier line, `REUSE.toml` states the licence of
   every other file, and `reuse lint` checks both in CI.
2. **The history has one author**, so as far as it shows there is one
   copyright holder, and the choice of licence is theirs [V: `git shortlog`].
3. **A Windows package ships Qt's runtime**, and now carries the third-party
   notices and the GPL text beside the programs (`NOTICES.txt`, `LICENSE.txt`).
   The command-line package carries no third-party code; the rest of what is
   used today is a build tool (vcpkg) and a test framework (Catch2), neither of
   which is in a package.
4. **Qt 6.12 is used by the application** (the optional window; the library and
   the CLI do not use it). The window links Qt Quick 3D for the 3D board scene, which Qt offers only under
   the GPL-3.0 or commercially, so a build of the window is **GPL-3.0 only**
   (the author's recorded decision; see [Qt](#qt)). The library and the CLI stay
   free of Qt and of that consequence. A package of the window says so plainly
   in its notices.
5. **The English word list is ENABLE**, a separate public-domain work kept
   unmodified in `data/dictionaries/` (see [The word list](#the-word-list)).
6. **The history begins with the author's own 2013 program**, written before
   this licence was chosen. Third-party files committed then (a vendored OCR
   build tree and prebuilt libraries, build output, and a word list whose origin
   was not recorded) were removed from every commit before the repository was
   published (see [History](#history)).

## The project's own licence

WordJewelsSolver is licensed under the GNU General Public License, version 3,
or (at your option) any later version. The text is in `LICENSE` and is the
unmodified GPLv3. `vcpkg.json` says `GPL-3.0-or-later`, and so does this page.

- **Authorship.** Every commit is by Iain Chesworth, under several email
  addresses, from 2013 and from 2026 [V: `git shortlog -sne`]. A copyright
  holder may add permissions, dual-license or re-license their own code [I][L].
- **Contributions** are accepted under the same terms, GPL-3.0-or-later. There
  is no contributor agreement.
- **Not covered by that statement:** the third-party components below, and the
  code from before the licence was chosen ([History](#history)).
- **App stores.** Nothing has been released and there are no store listings
  yet, so GPLv3's friction with some stores' terms is not a live question for
  the first release. A ZIP that a CI run produces is a package, not a
  distribution. It will be a live question when the tablet and phone targets in
  the
  [product requirements](product-requirements.md) and the
  [roadmap](roadmap.md) are built: the Apple App Store in particular. The
  analysis to repeat is the one in Cubeulator's
  [LICENSE-NOTE.md](https://github.com/iainchesworthlabs/cubeulator/blob/main/LICENSE-NOTE.md)
  and [licensing paper](https://github.com/iainchesworthlabs/cubeulator/blob/main/docs/licensing.md).

## Inventory

| Component | Licence | Role | In a build you distribute |
| --- | --- | --- | --- |
| WordJewelsSolver (`libs/`, `apps/`, `tests/`, `tools/`, `cmake/`, `docs/`) | GPL-3.0-or-later [V] | This project | Yes |
| `apps/app/translations/*.ts` (the interface's catalogues) | GPL-3.0-or-later, the project's own text [V] | The eight languages' wording | Yes, compiled into the application |
| vcpkg (the labs' fork, a separate clone found through `VCPKG_ROOT`) | MIT, Copyright Microsoft Corporation [V: `LICENSE.txt` in the vcpkg clone] | Fetches and builds dependencies | No: a build tool |
| vcpkg helper ports `vcpkg-cmake`, `vcpkg-cmake-config` | MIT [V: installed `share/*/copyright`] | Build helpers | No |
| Catch2 3.15.3 | BSL-1.0 [V: installed `share/catch2/copyright`] | The test framework (`tests` vcpkg feature) | No: tests only |
| Qt 6.12 (Core, Gui, Qml, Quick, Quick Controls 2, Quick 3D) | Commercial, LGPL-3.0, GPL-2.0 or GPL-3.0 for the first five; **Quick 3D: GPL-3.0 only** [I] | The optional Qt Quick window; not built by vcpkg. A build of the window is GPL-3.0 only | Yes, in a Windows package: the DLLs, plugins and QML modules the window loads, with the LGPL text in `NOTICES.txt` |
| Qt 6.12 Multimedia, Sensors and TextToSpeech | LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only [V: `<qt>/sbom/qt{multimedia,sensors,speech}-6.12.0.spdx` in the shared kit] | The feedback engines: short sounds, the device's speech, the accelerometer/gyroscope. Optional at build time (silent variants) | Yes, in a Windows package when the kit has them: the DLLs and their plugins, under the LGPL option |
| The project's feedback sounds (`assets/sounds/*.wav`) | GPL-3.0-or-later, the project's own work; synthesised placeholders for the reviewer's recordings [V: `tools/sounds/make_sounds.ps1`] | The eight cue assets | Yes, embedded in the application |
| Tracy 0.14.1 (a vcpkg port) | BSD-3-Clause [V: the port's installed `copyright`, reproduced in `notices/fragments/tracy.txt`] | Profiling, in the Debug and `*-profiling` presets only. It opens a TCP listener ([Profiling](profiling.md#the-profiler-listens-on-the-network)) | **Never in a release.** The release presets build without it and `cpack` refuses a Tracy build; a development package (a Debug build, which is what CI uploads) carries `TracyClient` and a Tracy section in its notices, added to `NOTICES.txt` only when the build has Tracy |
| The camera: Qt Multimedia (above); the letter reader | Qt Multimedia as above; the reader is the project's own code | Mode A's live picture on the desktops, and the reader | Yes: the Multimedia DLLs and plugin with the rest of Qt, and the project's code. No OpenCV, Vision or trained model is used yet |

Ports are listed with the licence in their installed `copyright` file. The
notices a package carries are assembled from what actually ships
([Notices](#notices)), not from this table.

## Notices

Every package carries `NOTICES.txt` beside the programs, and the application
embeds the same text as `:/notices/NOTICES.txt`, which its About screen shows
under Licences. The GPL text is installed beside it as `LICENSE.txt` (the
project's `LICENSE`, unmodified).

The file is assembled at configure time by `cmake/Notices.cmake` from the
fragments in `notices/fragments/`, so the versions it names are the ones the
build used and a package cannot be made without it. Its layers:

- the project's own licence, GPL-3.0-or-later, and where the source is;
- **the application binary is GPL-3.0-only**, because it links Qt Quick 3D,
  which Qt offers only under GPL-3.0 or commercially ([Qt](#qt)). The notice
  says so in capitals, next to the "or later" statement, so the two cannot be
  confused;
- Qt 6.12: the modules used under the LGPL (Core, Gui, Qml, Quick, Quick
  Controls 2, Multimedia, Sensors and TextToSpeech), the options Qt also offers
  them under (GPL-2.0/3.0 or commercial), the LGPL-3.0 text, how the libraries
  can be replaced (they are linked dynamically and found beside the program
  through `qt.conf`), where Qt's corresponding source is published, and that no
  Qt translation catalogue is installed;
- Qt Quick 3D, GPL-3.0-only, and that it is why the binary is;
- ENABLE, the public-domain word list ([below](#the-word-list)), with its
  SHA-256 and the pointer to `data/README.md`;
- the interface's translations, which are this repository's own text;
- the bundled light probe, the project's own, and the three bundled typefaces
  (Archivo, Andika and IBM Plex Mono), each under the SIL Open Font License with
  its copyright and licence text reproduced in the notices;
- Catch2, BSL-1.0, used to build and test and **not shipped**.

The `notices.sections` test holds the generated file to those sections in every
build, and fails if a placeholder is left in it. The fragments are the place to
change a sentence; the generated file is not edited. A third-party component
that starts shipping means a new fragment, its licence text in
`notices/licences/` if the terms ask for it, and the phrase the test asks for.

## Qt

Qt is a prebuilt dependency found through `CMAKE_PREFIX_PATH` or `QT_ROOT_DIR`;
it is not part of the repository and is not built by vcpkg. Today only the
optional window (`WJS_BUILD_APP`) uses it. The modules it links are Core, Gui,
Qml, Quick, Quick Controls 2, Multimedia, Sensors and TextToSpeech, which Qt
offers under the LGPL-3.0 as well as the GPL (2 or 3) and a commercial licence,
and **Quick 3D**, which Qt offers only as commercial or GPL-3.0-only: the board
scene is 3D, so a build of the window can be conveyed only under GPL-3.0 and
its "or later" stops there. That is the author's decision of 2026-10-16 for the
private MVP build, and D2 (in the [roadmap](roadmap.md)) decides what a store
build does; the source files stay GPL-3.0-or-later either way. The three
feedback modules are optional at configure time: a kit without them builds the
silent variants, so no package claims a module it does not carry. The kit's
`sbom/qtmultimedia-6.12.0.spdx`, `qtsensors-...` and `qtspeech-...` are the
record of their licence expressions.

The rule for adding another module is still to check how Qt offers it first, in
"Qt licensing" in the Qt 6.12 documentation, and to avoid modules that are
offered only as **GPL-3.0-only** (or commercially) in anything that is to keep
the "or later". Quick 3D is the one deliberate exception, made with the decision
and the seam above; Qt Quick 3D Physics, on the same GPL-only list, is not used.
A module offered under the LGPL does not raise it. `<qt>/sbom/*.spdx.json` in a
Qt install holds the licence expression of every module in that install.

Qt's build tools (`moc`, `rcc`, `windeployqt` and others) are GPL-3.0 with
Qt's GPL exception, and are not shipped; the exception covers their output [I].

A package of the window on Windows ships those LGPL modules, and meets the
obligations that come with them: the LGPL-3.0 text travels in `NOTICES.txt`
([Notices](#notices)), the libraries are linked dynamically and can be replaced
with a binary-compatible Qt 6 (they are loaded from beside the program through
the `qt.conf` the install-time deploy writes), and Qt's corresponding source is
named where it is published. The application embeds the notice, so the About
screen shows it without a file to find. The Linux `.deb` and `.rpm` are the
command-line solver only and carry no Qt; the Linux application is an AppImage that
bundles Qt, and the macOS application is a bundle with Qt deployed into it. Both are
built by `release.yml` and **have never run**, so how they meet the replaceability
and notice obligations is unverified ([CI](ci.md)).

## The word list

The solver needs a word list. The one in the repository is **ENABLE**
(`enable1.txt`, 172,823 words), kept unmodified as `data/dictionaries/en.txt`.
Its originators released it into the public domain; where it was fetched from,
when, and its SHA-256 are in [data/README.md](https://github.com/iainchesworthlabs/wordjewelssolver/blob/main/data/README.md).
That the notice is genuine is taken from the list's published statement and has
not been independently verified [I][L]. `wjs-cli` and the window look for
`dictionaries/en.txt` beside the executable, and a build copies it there.

The sentences that follow are why the choice of list was a decision at all, and
what any replacement list has to satisfy.

Why it matters: a word list is data with its own licence, separate from the
program's. Some lists are in the public domain or under permissive licences;
others forbid redistribution or carry terms that do not fit a GPL-3.0-or-later
program shipped with its source. The decision has to settle, at least:

- that the licence allows redistributing the list alongside this program, and
  its source, and that the licence text and any attribution travel with it;
- where the list came from, written down;
- whether a list made for a word game for adults is the right list for a child
  of five or six playing with an adult (a long list is full of words neither
  of them has seen, and a short one will say that a real word is not a word);
- which English: spellings differ between countries.

The word list the original 2013 program used is not part of this repository:
nothing recorded where it came from.

Rules for word lists:

- **do not add a word list without recording its source, date and licence** in
  `data/README.md`, and keep a list that is someone else's work unmodified so its
  hash can be compared;
- the child-facing category and tier lists under `data/lexicon/` are written for
  this project and are subsets of the dictionary, checked by
  `tools/data/check_wordlists.ps1`;
- the tests use lists of a dozen words written in the test itself;
- a list you supply with `-d` is yours, under its own terms; the program reads
  it and does not copy it anywhere.

## Fonts and the interface's languages

**Three typefaces are bundled**, all under the SIL Open Font License 1.1:

- **Andika** (Regular, Bold), SIL Global, for the reading text and the dice;
- **Archivo** (Regular, SemiBold, ExtraBold), The Archivo Project Authors, for
  the interface's headings, buttons and labels;
- **IBM Plex Mono** (Medium), IBM Corp., for small uppercase labels and counts.

Their copyright lines and full licence texts are in
`notices/fragments/fonts.txt`, which is reproduced in the generated
`NOTICES.txt`; the original OFL files stay beside the typefaces in
`assets/fonts/`. The files are compiled into the application's resources as
`:/fonts/...` and are not installed separately.

Nothing of Microsoft's, Apple's or Google's is copied into the repository or a
package: Segoe UI/Arial (Hebrew), Microsoft YaHei and Yu Gothic (Chinese and
Japanese) are the system fallback on Windows, PingFang and Hiragino on the iPad,
and CJK faces are deliberately **not** bundled because of their size
([Application design](design/application.md#languages-right-to-left-and-fonts)).
If a Hebrew or Yiddish glyph is ever missing from a device's chain, Noto Sans
Hebrew (OFL) is the candidate: it would be added under an `assets/` directory,
the OFL text and its notice recorded here, and the file added to the resource
and the font family list. It is not in the tree today, so it has no entry in the
inventory.

**The interface's translations** (`apps/app/translations/*.ts`) are this project's own text, under
the project's licence, like the English wording they translate. No third-party
translation file is used. Qt supplies the catalogue mechanism (`QTranslator`,
`lupdate`, `lrelease`) but no translated strings of its own; the Qt translation
catalogues (`qtbase_*.qm` and friends) are **not** installed or merged.

## Camera input and letter recognition

Both are built (`libs/scan`) and add **no third-party code beyond Qt**: the desktops'
camera backend is Qt Multimedia (already recorded above), and the reader is the
project's own C++23 over a byte buffer, matching glyphs against the project's own stroke
font (a drawing of the project's, not a typeface of anyone's), with no trained model and
no data file. So nothing here has a further licence to record yet. An OpenCV-backed reader, a Vision
reader on the iPad, or a font or model trained from the real dice would each add one
([Scanner design](design/scanner.md#no-image-dependency)). When one is added:

- record the licence of every library, and also of any trained model or data
  file: they often carry terms of their own, different from the code's, and
  some are not redistributable at all;
- fetch such files at build or first run rather than vendoring them. The
  `.gitignore` already excludes `*.traineddata` and `*.onnx` on that footing;
- a picture of a person must never be committed, and this application is meant
  to be pointed at a game played with children. `/tests/data/captures/` and
  `/docs/evidence/` are git-ignored for that reason, and a test that wants a
  recording must find it outside the tree and skip when it is not there.

## History

The history begins on 2013-06-05 with the original Windows program, which is
the author's own code, written before this licence was chosen. Third-party
files committed in that period (a vendored OCR build tree with prebuilt
libraries, build output, and a word list whose origin was not recorded) were
removed from every commit before this repository was published, so none of them
is in the history or in any release. The tag `legacy-win32-2026-10-15` marks
the last commit of the original Win32 and DirectShow program.

## What a lawyer should confirm

- That distributing a build of this program together with Qt's LGPL modules,
  under GPL-3.0-or-later, meets both sets of terms, in the packaging that is
  eventually chosen.
- The origin and terms of the 2013 word lists, and, if the history were ever
  published with them in, whether it may be.
- Anything to do with an app store, before a submission.
