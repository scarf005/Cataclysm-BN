#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
installer="${SHADERCROSS_INSTALLER:-${script_dir}/install-shadercross-vcpkg.sh}"
fixture_dir="$(mktemp -d)"
trap 'rm -rf "${fixture_dir}"' EXIT
upstream="${fixture_dir}/upstream"
checkout="${fixture_dir}/checkout"
mkdir -p "${upstream}" "${fixture_dir}/bin"
# Bootstrap is inert here, so dependency discovery must not require installed autotools.
for tool in autoconf automake libtoolize; do
    printf '#!/usr/bin/env bash\nexit 0\n' > "${fixture_dir}/bin/${tool}"
    chmod +x "${fixture_dir}/bin/${tool}"
done
export PATH="${fixture_dir}/bin:${PATH}"
printf '#!/usr/bin/env bash\nexit 0\n' > "${upstream}/bootstrap-vcpkg.sh"
printf '#!/usr/bin/env bash\nexit 0\n' > "${upstream}/vcpkg"
mkdir -p "${upstream}/installed/x64-linux/tools/sdl3-shadercross"
printf '#!/usr/bin/env bash\nexit 0\n' > "${upstream}/installed/x64-linux/tools/sdl3-shadercross/shadercross"
chmod +x "${upstream}/vcpkg" "${upstream}/installed/x64-linux/tools/sdl3-shadercross/shadercross"
git -C "${upstream}" init --quiet
git -C "${upstream}" add .
git -C "${upstream}" -c core.hooksPath=/dev/null -c user.name=Fixture -c user.email=fixture@example.invalid commit --quiet -m 'chore: initialize fixture'
pin="$(git -C "${upstream}" rev-parse HEAD)"
unset GITHUB_ENV GITHUB_PATH GITHUB_OUTPUT
export GIT_CONFIG_GLOBAL="${fixture_dir}/gitconfig"
export GIT_CONFIG_NOSYSTEM=1
git config --file "${GIT_CONFIG_GLOBAL}" "url.${upstream}.insteadOf" https://github.com/microsoft/vcpkg

# A real no-checkout clone already has HEAD, but no bootstrap script or index.
git clone --quiet --no-checkout "${upstream}" "${checkout}"
test "$(git -C "${checkout}" rev-parse HEAD)" = "${pin}"
test ! -f "${checkout}/bootstrap-vcpkg.sh"
VCPKG_COMMIT="${pin}" bash "${installer}" "${checkout}" x64-linux

test -f "${checkout}/bootstrap-vcpkg.sh"
test "$(git -C "${checkout}" rev-parse HEAD)" = "${pin}"
# A second setup must preserve and successfully reuse the pinned checkout.
VCPKG_COMMIT="${pin}" bash "${installer}" "${checkout}" x64-linux
printf 'Pinned HEAD no-checkout and warm reuse passed\n'
