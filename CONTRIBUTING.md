# Contributing

## Development Flow

Use `main` as the integration branch. Create a short-lived branch for each change and open a pull request back to `main`.

Keep pull requests focused. Prefer rebasing or squash merging so the public history remains linear and each commit represents a complete change. Do not create synchronization branches for other remotes.

By submitting a contribution, you agree to make that contribution available under the project's [GNU Affero General Public License v3.0](LICENSE) (`AGPL-3.0-only`).

## Quality Checks

Pull requests must pass the `Quality` workflow:

- `clang-format --dry-run --Werror` for project C and C++ files.
- `clangd --check` using the generated compilation database.
- A complete Clang Windows build.
- CTest with failure output enabled.

Do not format or otherwise modify vendored submodule sources as part of Launcher changes.

## Releases

1. Update the version in `CMakeLists.txt`.
2. Merge the release-ready change into `main`.
3. Create an annotated tag matching that version, for example `0.9.24`.
4. Push the tag to GitHub.

The `Release` workflow verifies that the tag and CMake version match, builds and tests Launcher, creates a ZIP archive plus `SHA256SUMS.txt`, and publishes both to the GitHub release for that tag.
