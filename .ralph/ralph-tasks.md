- [x] Audit active release and PR workflows for non-CMake build steps
- [x] Close my open CMake migration PRs
- [x] Change CMake formatting targets to only print changed files
- [x] Migrate macOS workflow from `make dmgdist` to CMake-based packaging
- [x] Migrate Android native build from `ndk-build` and `make` hooks to CMake-based flow
- [x] Ensure Android fork builds work without signing secrets
- [x] Confirm iOS status and make keyless behavior explicit
- [x] Update release workflows to deploy only CMake-based outputs
- [x] Update docs to make CMake canonical and retire non-CMake guidance
- [x] Run verification and compare hashes against legacy outputs where possible
- [/] Commit atomically and push to origin

## Verification Notes

- Linux CMake configure check passed with `cmake -S . -B /tmp/opencode/cmake-release-verify -G Ninja -DCMAKE_BUILD_TYPE=Release -DTILES=OFF -DCURSES=ON -DSOUND=OFF -DTESTS=OFF -DLANGUAGES=es_ES -DJSON_FORMAT=OFF -DUSE_PREFIX_DATA_DIR=OFF`.
- Legacy `lang/compile_mo.sh` and new `build-scripts/android_localizations.cmake` produced byte-identical `es_ES` catalogs.
- Legacy-style Makefile version header content and `src/version.cmake` output produced identical hashes.
- Active workflows no longer reference `dmgdist`, `ndkBuild`, `make localization`, or `make version` under `.github/workflows/*.yml`.
- Byte-for-byte package comparison was not possible for macOS artifacts in this Linux worktree because the active packaging path now runs `package-dmg` in `.github/workflows/build.yml` and `.github/workflows/osx.yml`, while DMG creation is macOS-specific in `build-scripts/package_macos.cmake`.
- Byte-for-byte package comparison was not possible for Android APK/AAB artifacts here because the repository no longer contains the legacy `ndkBuild` workflow path; current Gradle packaging delegates native compilation to `externalNativeBuild.cmake` in `android/app/build.gradle`, which requires the Android SDK/NDK environment not present in this worktree.
- Byte-for-byte package comparison was not possible for iOS because this repository still has no iOS workflow or Xcode project, as documented in `docs/en/dev/guides/building/cmake.md`.
