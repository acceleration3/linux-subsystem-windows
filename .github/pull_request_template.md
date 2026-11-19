## What this changes

<!-- What the change does, and why. Link the issue it fixes: "Fixes #123". -->

## How it was tested

<!-- What you ran or tried, and on which distribution, desktop and GPU. -->

## Checklist

- [ ] `ctest --test-dir build --output-on-failure` passes.
- [ ] C++ is formatted with the repo's `.clang-format`.
- [ ] Anything the manager and the guest agent must agree on is in `src/linkproto/`, and both sides are updated.
- [ ] GPU changes are in `accel-virt` and window changes in `remotewin`, with the submodule pointer bumped here.
- [ ] User-facing text follows the existing wording, and the README is updated if a feature changed.
