# Barge releases and identity

Barge is an independent project derived from Dinghy. Build and host-test the baseline first with `./build.sh` and
`tests/run_host.sh` (see the [README](../README.md)).

## Project identity

FM-1 installers identify a firmware by its package marker, so different projects should not reuse one. Barge uses
the `FM-1_96N` family (`FM-1_960` for the development build); `tools/build.py` derives the release identity.
Check the current Firmware Hub directory before reserving a release identity for distribution.

The application display name is `BARGE`. Build outputs use `barge` (`build/barge.fwsc` or
`build/barge-X.Y.fwsc`). Barge's mark is `assets/logo.svg`.

## Release

```sh
./build.sh --release 0.1        # -> build/barge-0.1.fwsc, identity FM-1_961
```

Publish a tagged GitHub release with the `.fwsc` and its source. The Firmware Hub can list public releases; see
its *Made an open-source firmware?* card for current submission instructions.

## Module contributions

Barge intends to support selectable instrument modules while keeping shared platform services independent.
Before adding an engine, document its origin, license, attribution, memory and CPU cost, and which module API it
uses. The first implementation should link selected modules into the image; do not assume dynamic loading.

## Keep the credits

Barge derives from Dinghy, whose platform carries work from Felucca (Leo Kuroshita, Hügelton Instruments) and
SLOOP (isod89). Keep inherited copyright headers and `LICENSING.md`, preserve third-party license terms, and add
appropriate attribution for new contributions.
