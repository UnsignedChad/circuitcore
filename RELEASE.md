# Cutting a release

Releases are driven by Git tags. The
`.github/workflows/release.yml` workflow watches for `v*` tags and a
manual `workflow_dispatch` button; nothing in `git push` (no tag) or a
PR merge ever triggers it.

## What gets built

| Platform | Format        | Contents                                          |
|----------|---------------|---------------------------------------------------|
| Linux    | `.AppImage`   | `circuitcore_studio` + tools, Qt bundled          |
| Windows  | `.zip`        | `circuitcore_studio.exe` + tools, Qt DLLs bundled |
| macOS    | `.dmg`        | `CircuitcoreStudio.app` (ARM64) + tools           |

All three artifacts are attached to the same GitHub Release page.

## Releasing

1. Decide a version. Use SemVer: `vMAJOR.MINOR.PATCH`. A pre-release
   suffix (anything after a `-`) marks the release as a "Pre-release"
   on GitHub.

2. Tag locally:

       git tag v0.1.0
       git push origin v0.1.0

3. The workflow runs (~10 min wall-clock). When it finishes, a
   **draft** release exists with the three artifacts attached. The
   draft step is deliberate -- you review the artifacts before
   publishing.

4. On the GitHub Releases page, edit the draft, add any release notes
   the auto-generator missed, and click **Publish**.

## Dry-runs (no release, just artifacts)

Trigger the workflow manually from the Actions tab. Skip the
release step -- the `release` job is gated on `startsWith(github.ref,
'refs/tags/v')`, which is false on `workflow_dispatch`. You still get
the artifacts attached to the workflow run (downloadable for 14 days)
without publishing anything user-visible.

Use this to validate that all three platforms still build after a
risky change.

## Pulling a release

```
git tag -d v0.1.0
git push origin :v0.1.0
```

Then delete the Release from the GitHub web UI.

## First-time-only notes

- **macOS bundle is unsigned.** First time users open it, Gatekeeper
  refuses with "can't verify developer". Right-click the .app and
  choose Open; macOS will remember the override. Proper notarization
  needs an Apple Developer Account ($99/yr) -- worth setting up before
  v1.0.
- **Linux AppImage** needs FUSE to mount itself. On bare-bones server
  installs, `--appimage-extract` is the fallback.
- **Windows zip** is portable; unzip and run `circuitcore_studio.exe`
  from wherever. No installer yet.
