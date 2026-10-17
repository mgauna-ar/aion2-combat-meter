---
name: release
description: Cut a release of the AION 2 meter and keep it installable by every copy already out there. Use when bumping the version, tagging a v*.*.* release, changing the release zip, the CPack name or the install() rules, or diagnosing the release job or a copy that does not update - including any change to src/app/include/app/update_check.hpp.
---

# Releasing

Installed copies update themselves from this repository's `releases/latest`
(AGENTS.md, *Self-update*). A release is what every one of them downloads, so it has to
look exactly the way they were built to expect.

## 1. Before tagging

1. Run `make test`, `make asan` and `make tsan` on the Mac.
2. Set the number in `include/aion2/version.hpp`, the only place it is written:
   - `AION2_VERSION_MAJOR`, `_MINOR` and `_PATCH`, and `AION2_VERSION_STRING`;
   - a `static_assert` fails the build when the string does not spell the numbers.
   - CMake parses the numbers for `project(VERSION)` and CPack, `app.rc` includes the
     file for the executable's version resource, and the sidebar shows it.
3. Commit (`chore: release vX.Y.Z`) and push `main`. Wait for CI to pass: its
   `build-windows` job is the only MSVC build, and the first to compile
   `src/app/src/win32/`.

## 2. Tag

```bash
git tag vX.Y.Z
git push origin vX.Y.Z
```

The `release` job in `.github/workflows/ci.yml` runs only on such a tag, after every
other job has passed. It checks the tag (less its `v`) against `AION2_VERSION_STRING`,
refuses one that disagrees, then publishes the zip and `SHA256SUMS.txt` with generated
notes. Tag runs are never cancelled.

## 3. Check what was published

```bash
gh release view vX.Y.Z --json isDraft,isPrerelease,assets --jq '{isDraft, isPrerelease, assets: [.assets[] | {name, digest}]}'
```

- `isDraft` and `isPrerelease` are false.
- `aion2-combat-meter-windows-x64.zip` has a `sha256:` digest.

Then, without credentials, what an installed copy sees:

```bash
curl -s https://api.github.com/repos/mgauna-ar/aion2-combat-meter/releases/latest | jq '{tag_name, draft, prerelease, assets: [.assets[] | {name, browser_download_url, digest}]}'
```

A 404 means the repository is private, or nothing is published: no copy can see the
release.

On the Windows PC, an older copy's **Settings → Updates → Check now** finds the new
version, and **Update & restart** comes back on it ("Updated to version X.Y.Z").

## What the updater expects of a release

The constants are in `src/app/include/app/update_check.hpp`. Older versions keep looking
for exactly what they were built to find:

- **The tag** is `vMAJOR.MINOR.PATCH`, and the release is published, neither a draft
  nor a prerelease: `releases/latest` skips both, so a prerelease reaches no one.
- **The asset** is named `aion2-combat-meter-windows-x64.zip`, CPack's
  `CPACK_PACKAGE_FILE_NAME`, and is attached to this repository's release, so its URL
  lies under `https://github.com/mgauna-ar/aion2-combat-meter/releases/download/`.
- **The executables** `aion2-combat-meter.exe` and `aion2meter-replay.exe` sit at the
  archive's root (`CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF`), stored or deflated, without
  Zip64. Both must be there: an update is all or nothing.
- **GitHub's asset digest** is how the download is verified. A release without one is
  announced but cannot be installed from the app.
- **The repository** is public: the API shows no anonymous caller a private one's
  releases.

Renaming the asset, nesting the executables in a folder, renaming an executable,
moving the repository, or a new tag scheme strands every copy already installed. They
would have to update by hand. A change to any of them needs a release that still ships
the old shape, and a way for old copies to reach the new one.

## The archive's contents

The `install()` rules in `CMakeLists.txt` are the only definition: CI packages with
`cpack --config build/CPackConfig.cmake -G ZIP -C Release -B dist`, so a release and a
local `cpack` run on Windows are the same zip. A file the release should ship is an
`install()` rule, never a copy step in the workflow. The updater takes only
`UPDATE_FILES` out of it; README and LICENSE are there for whoever unzips it by hand.

The workflow's token is `contents: read`; only the `release` job gets `contents: write`.
