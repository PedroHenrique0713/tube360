# Org builds and release drafts

`CI` always starts a read-only gate, even for documentation changes. Relevant PRs and pushes build PC, run local contracts and standalone/update tests, then build the Xbox app and launcher. Set the required check to `CI / required`; source path filters live inside the gate so documentation-only PRs finish explicitly. Superseded PR runs cancel; release runs do not.

Both build workflows consume the exact public OCI digest in [devkit-image.txt](../.github/devkit-image.txt). The input job rejects tags, other registries and missing digests before starting a container. Only record a digest after verifying the devkit image's provenance and anonymous availability. The initial F55 rollout leaves this file pending until that image exists; it must never silently use a developer compiler or a mutable image tag.

The image exports its toolchain/library paths. `tools/ci/build.sh` isolates downloads/builds in the fresh Actions checkout, uses two compiler workers and disables local build.env loading. It copies pinned mbedTLS/curl source from the image and passes its prebuilt Xbox FFmpeg using `FFMPEG_PREFIX`; changed FFmpeg recipes require a newly reviewed devkit image. mbedTLS/curl still compile with the app's current configuration. No PR-produced caches feed release builds. Native and Xbox build directories are separate. No CI step contacts a console, Nest speaker or Bluetooth device.

## Proving a branch without creating a tag

The temporary F55 push triggers (`f55-ci` in devkit, `f55-ci-release` here) allow new workflows to run before they exist on default branches. GitHub manual dispatch requires a workflow to exist on the default branch; the proof does not merge either branch to enable it. A branch release proof uses version `0.3.3-ci.<run-id>`, builds/packages/attests, and retains an Actions artifact for three days. It creates no tag and no release. Download it promptly:

```sh
gh run download <run-id> --repo revive360/tube360 --name tube360-0.3.3-ci.<run-id> --dir evidence
(cd evidence && sha256sum -c SHA256SUMS)
gh attestation verify evidence/tube360-v0.3.3-ci.<run-id>-xbox360.zip --repo revive360/tube360
gh attestation verify evidence/tube360-v0.3.3-ci.<run-id>-xbox360.7z --repo revive360/tube360
gh attestation verify evidence/tube360.xex --repo revive360/tube360
```

The saved `attestation.jsonl` can also be supplied with `--bundle evidence/attestation.jsonl`. Check the source commit/ref and workflow in the verification output, not only the repository name. A branch proof does not certify reviewed release source or real hardware.

## Preparing a release for review

An existing `v<semver>` tag starts `Release draft`; manual dispatch on an existing tag does the same. The tagged commit must be an ancestor of `origin/main`. Tag creation and merge/review remain maintainer work. The workflow never creates tags. Build, test, packaging, hashes and attestation run in the pinned container. The packager produces both archives and the app XEX; `default.xex` in each archive is the distinct launcher. `package-test.py` checks matching ZIP/7z bytes, manifests, production keys and marker restrictions.

The attested assets also include the exact Git source, dependency source (private patched xsdk/vulkan4xenon, SDL and upstream archives), and a relink kit. `source-kit.py` collects the actual CMake link inputs, including launcher platform and boot objects, and compares replayed ELFs byte for byte before archiving. `CI-BUILD.json` records the source, image, run URL and pending hardware test. `SHA256SUMS` covers those assets. No private signing key is needed or included; build provenance uses Actions OIDC.

The attestation job has only `contents: read`, `id-token: write`, `attestations: write`. A separate host job receives `contents: write` to create/upload the **draft** from the already-existing tag. It refuses a public release and does not overwrite assets on reruns. A three-day artifact bridges the two jobs; public distribution belongs in Releases. All Actions use full commit pins. Rerun a failed release with unchanged inputs; do not replace an already-reviewed asset.

The normal packager still embeds the pinned production update verification key; provenance does not sign the separate in-app update protocol. Before enabling a public update, review the monotonic `TUBE360_BUILD_ID` (currently this workflow's run number) against all previously distributed builds.

## What the Official/Project record still needs

CI alone does not grant a badge. A maintainer must test the **exact downloaded CI ZIP/7z/XEX hash** on a real console, record hardware/result/hash, then review the source and draft assets. No local rebuilt binary can replace that artifact. The catalog record must use `builder: org-ci`, the run URL and attestation URL; its current release must equal the reviewed artifact. Populate the real non-example catalog entry only after this evidence exists. This front does not change the catalog or publish a release.
