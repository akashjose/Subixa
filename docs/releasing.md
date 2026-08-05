# Releasing

How a version number gets chosen, and how the three downloads get built.

Nothing here is run by hand. A release is a pull request you merge; the number
in it was computed from the commit messages, and the downloads are built by the
same workflow that builds every pull request.

## The short version

1. Land work on `master` through pull requests, with conventional-commit
   prefixes — `feat:`, `fix:`, and the rest of the table below.
2. A standing pull request titled *chore: release x.y.z* keeps itself up to
   date. It carries the version bump and the changelog entry it would write, and
   nothing else.
3. Merging it is the release. It tags, publishes the GitHub release, then builds
   and attaches the AppImage, the Windows portable zip and the Windows
   installer, each with a `.sha256` beside it.

Between step 1 and step 2 you can read the version the project is heading
towards at any time, which is most of the value: the number stops being a
decision anybody makes under time pressure.

## The prefix decides the version

`.commitlintrc.yml` enforces the prefixes and the lint job checks both the
commits and the pull request title, because either can be the one that reaches
`master` — the commits on a merge, the title on a squash.

| Prefix | Version | In the changelog |
|---|---|---|
| `feat:` | minor — 0.6.0 → 0.7.0 | Features |
| `fix:` | patch — 0.6.0 → 0.6.1 | Fixes |
| `perf:` | patch | Performance |
| `refactor:` | none | Refactoring |
| `docs:` | none | Documentation |
| `build:` | none | Build and packaging |
| `ci:`, `test:`, `style:`, `chore:` | none | hidden |

A `!` after the type, or a `BREAKING CHANGE:` footer, is a breaking change.
**While the version is below 1.0 that bumps the minor, not the major** — which
is the semver rule for 0.x, and worth knowing before you assume a breaking
change is being announced loudly.

Types that bump nothing still land on `master` normally; they simply do not open
a release pull request on their own. A run of pure `ci:` and `docs:` commits
produces no release, which is the honest outcome — there is nothing in it for
anyone to download.

## The version has one source

`version.txt` at the repository root. Everything derives from it:

- `CMakeLists.txt` reads it into `project(subixa VERSION ...)`, and from there it
  becomes `SUBIXA_VERSION` in the binary, the Windows resource block through
  `icons/subixa.rc.in`, and the About card through `setApplicationVersion`
- `tools/version.sh` reads it for the three package filenames, which need the
  number before a build exists

The one file that carries the version as data rather than deriving it is
`com.akashjose.Subixa.metainfo.xml`, because an AppStream `<release>` entry is
what it is for. release-please rewrites it in the same commit, and CMake checks
at configure time that the two still agree — `appstreamcli` validates the shape
of that entry and has no opinion about which version it names, so a metainfo one
release behind would otherwise pass CI, install, and tell every software centre
the wrong thing.

That entry carries no `date`, which is deliberate and cost the first release to
learn. release-please applies **one marker per line**, and XML forbids a comment
inside a tag, so the version marker and the date marker had to share the line —
the version moved on 0.6.0 and the date silently stayed at the previous
release's. AppStream treats the date as optional, and no date is better than a
confidently wrong one.

`CHANGELOG.md` is machine-owned for the same reason and is deliberately just its
heading. Anything else written under that heading is pushed below the newest
release entry and wrapped in a heading of release-please's own, drifting further
down with every release. Notes about how releases work belong in this file.

Do not edit any of them by hand.

## What gets built

`build.yml` is called by `ci.yml` on the way in and by `release.yml` on the way
out, so what is published is what was tested rather than a second implementation
of it. On a release it also builds:

| | |
|---|---|
| `Subixa-x.y.z-x86_64.AppImage` | `tools/make-appimage.sh` — bundles everything except glibc |
| `Subixa-x.y.z-win64-portable.zip` | `tools/make-portable-win.sh` — the staged bundle, extract and run |
| `subixa-x.y.z-win64-setup.exe` | `tools/make-installer-win.sh` — Inno Setup, shortcuts and an uninstaller |

The same three build on demand without cutting a release: run **CI** from the
Actions tab, and they arrive as workflow artifacts instead of release assets.

The glibc floor on the AppImage is the build host's — Ubuntu 24.04, so 2.38,
meaning Ubuntu 23.10+, Debian 13+ and Fedora 39+. `docs/roadmap.md` has the
measurement and what was learned validating it.

## Before the first release

**The repository has to let Actions open pull requests.** Settings → Actions →
General → *Allow GitHub Actions to create and approve pull requests*. Without
it the release-please step fails on permission rather than doing nothing, which
is at least loud.

There is nothing else to arrange. `.release-please-manifest.json` starts at the
0.5.0 the tree already carried, and the first branch to reach `master` adds the
AppImage and the Windows installer as `feat:`, so the first release computes as
0.6.0 on its own.

If you ever need a number the prefixes would not produce — a jump to 1.0.0, or
a version to match something outside the repository — put a footer in the body
of a commit that reaches `master`:

```
Release-As: 1.0.0
```

It applies to that commit alone, so nothing has to be undone afterwards. That
is why it is a footer rather than a key in `release-please-config.json`, which
somebody would have to remember to remove.

## The release pull request runs no CI

It has no checks, and this is a property of GitHub rather than a gap in the
workflows: an event raised by `GITHUB_TOKEN` does not trigger another workflow,
so a pull request that release-please opened starts nothing. `gh pr checks` on
it reports nothing at all rather than reporting a failure.

What that costs is the configure-time check on the metainfo version. It does run
— but in the build that follows the tag, so it reports a mismatch after the
release is published rather than before. Read the diff on the release pull
request instead: it is four files, and three of them are one line each.

Handing release-please a personal access token would restore the checks. That
trades a repository secret for them, and it has not been done.

## When it goes wrong

**The release exists but has no downloads.** release-please tags and publishes
before the build runs, because the build depends on knowing the tag. If the
build then fails, the release is real and empty. Fix the cause and re-run the
**Release** workflow: the upload steps use `--clobber`, so a partial upload is
replaced rather than colliding. This is also why `master` is built by CI on
every push — a release should be cut from a tree already known to build.

**Windows is below the Qt floor.** MSYS2 is a rolling repository and Subixa's
Windows floor is Qt 6.11. On ordinary CI the Windows job stands down with a
notice, because MSYS2 not having caught up is not a regression in this tree. On
a release it fails instead: publishing Linux-only downloads under a green tick,
silently, means the first person to learn of it is a Windows user finding
nothing to download.

**A commit landed with no prefix.** It is in `master` and in no changelog, and
it did not move the version. Nothing is broken and there is nothing to repair
retroactively; the lint job on the next pull request is what stops it recurring.
