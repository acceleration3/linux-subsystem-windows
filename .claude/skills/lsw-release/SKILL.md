---
name: lsw-release
description: Release a version of LSW - bump the version, write the release notes commit, prove the upgrade from the last release, build and check the packages locally, and know what the release workflow does after a push to master. Use when the user asks for a release, release notes, packages or a package check.
---

# Release LSW

## How a release happens

A push to master runs `.github/workflows/release.yml`. It reads the version from the `VERSION` line of
`CMakeLists.txt`. If `v<version>` is not released yet, it builds the guest half, the Arch, deb and rpm packages,
checks each package in a container, publishes the GitHub release and commits `lsw-bin <version>` to the AUR. If the
version is already released, the run only builds.

The release notes are the body of the commit whose subject is exactly `LSW <version>`. Without one, the workflow
lists the commit subjects since the last release.

## Steps

1. Bump `VERSION` in `CMakeLists.txt` and write the notes as one commit: subject `LSW 0.3.4`, body in Markdown
   (`## Fixes`, `## New`, `## Updating`), written for users and naming the issues it closes. Commit it with
   `git commit --cleanup=verbatim` so Markdown headings survive.
2. Prove the upgrade from master: `lsw-dev` `upgrade_test` (options `pause-vm`, and `make-baseline` the first time).
   It stops and restarts the real VM and manager and takes long: ask the user first.
3. Optionally build the packages locally: `package` with `targets` arch, deb or rpm. They build in the named
   distroboxes (`scripts/dist.sh --setup` makes them once) into `dist/`. `scripts/verify-package.sh <type> dist` checks
   one, as root in a throwaway container.
4. The user pushes to master. Watch the run with `gh run watch`, then answer and close the issues the release fixes,
   linking the release.

## Rules

- Packaging builds happen in distroboxes, never in raw podman or docker.
- The release workflow publishes; do not create releases or AUR commits by hand.
- No AI attribution in commits, notes or issue replies.
