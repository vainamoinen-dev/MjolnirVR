# Code signing policy

## Status

Release files are not signed with an Authenticode certificate, and no signing
is planned. Windows SmartScreen therefore shows "Windows protected your PC" the
first time the launcher runs; choose "More info" and "Run anyway" after
checking the file as described below. Trust comes from the open source, the
build on GitHub's runner, the attestation and the hashes, not from a
certificate.

## How releases are produced

- Every release is built by the `release` workflow in this repository
  (`.github/workflows/release.yml`) on a GitHub-hosted Windows runner, from
  the tagged commit, with the unit tests run first. No release file is built
  on a developer's machine.
- Each published file carries a GitHub build provenance attestation that ties
  it to the workflow run and the commit. Verify with
  `gh attestation verify <file> -R vainamoinen-dev/MjolnirVR`.
- `SHA256SUMS.txt` in every release lists the hash of every file.
- The launcher verifies each payload file against `INSTALL-MANIFEST.sha256`
  before it installs anything.

## Privacy

The program transfers no information to any networked system. See
`docs/WHAT-IT-TOUCHES.md`.

## Roles

- Committers and reviewers: the project maintainers listed on the repository.
- Approvers of a release: the maintainer who pushes the release tag.

## Scope of a future signature

If a signing route is adopted, the signed files will be `HaloMCCVRLauncher.exe`
and `HaloMCCVR.dll` as produced by the workflow above, and only those.
