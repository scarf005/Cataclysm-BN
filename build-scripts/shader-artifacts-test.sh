#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "${test_dir}"' EXIT

source_dir="${test_dir}/src"
output_dir="${test_dir}/out"
mkdir -p "${source_dir}"
printf 'first shader\n' > "${source_dir}/first_compute.hlsl"
printf 'second shader\n' > "${source_dir}/second_compute.hlsl"

fake_shadercross="${test_dir}/shadercross"
cat > "${fake_shadercross}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
output="${!#}"
printf 'compiled %s\n' "$*" > "${output}"
EOF
chmod +x "${fake_shadercross}"

SHADERCROSS="${fake_shadercross}" \
    bash "${repo_root}/build-scripts/generate-shaders.sh" "${source_dir}" "${output_dir}"
bash "${repo_root}/build-scripts/verify-shader-artifacts.sh" "${source_dir}" "${output_dir}"

printf 'changed shader\n' >> "${source_dir}/first_compute.hlsl"
if bash "${repo_root}/build-scripts/verify-shader-artifacts.sh" "${source_dir}" "${output_dir}"; then
    echo "verification accepted a stale shader artifact" >&2
    exit 1
fi

SHADERCROSS="${fake_shadercross}" \
    bash "${repo_root}/build-scripts/generate-shaders.sh" "${source_dir}" "${output_dir}"
rm "${output_dir}/second_compute.spv"
if bash "${repo_root}/build-scripts/verify-shader-artifacts.sh" "${source_dir}" "${output_dir}"; then
    echo "verification accepted an incomplete shader artifact set" >&2
    exit 1
fi

echo "shader artifact generation and verification tests passed"
