#!/usr/bin/env bash
set -euo pipefail

source_dir="${1:-src/shaders}"
output_dir="${2:-data/shaders}"
manifest="${3:-${output_dir}/shader-artifacts.manifest}"

if [ ! -d "${source_dir}" ]; then
    echo "Shader source directory not found: ${source_dir}" >&2
    exit 1
fi
if [ ! -f "${manifest}" ]; then
    echo "Shader artifact manifest not found: ${manifest}" >&2
    exit 1
fi

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

manifest_version="$(grep -F 'shader-artifacts-version=' "${manifest}" | cut -d '=' -f 2- || true)"
manifest_targets="$(grep -F 'targets=' "${manifest}" | cut -d '=' -f 2- || true)"
if [ "${manifest_version}" != "1" ] || [ "${manifest_targets}" != "spv msl dxil" ]; then
    echo "Unsupported shader artifact manifest: ${manifest}" >&2
    exit 1
fi

shopt -s nullglob
shaders=("${source_dir}"/*.hlsl)
if [ "${#shaders[@]}" -eq 0 ]; then
    echo "No HLSL shaders found in ${source_dir}" >&2
    exit 1
fi

source_count="$(grep -c '^source=' "${manifest}" || true)"
artifact_count="$(grep -c '^artifact=' "${manifest}" || true)"
expected_artifact_count=$(( ${#shaders[@]} * 3 ))
if [ "${source_count}" -ne "${#shaders[@]}" ] || [ "${artifact_count}" -ne "${expected_artifact_count}" ]; then
    echo "Shader artifact manifest is incomplete" >&2
    exit 1
fi

for shader in "${shaders[@]}"; do
    shader_file="$(basename "${shader}")"
    expected_source="source=${shader_file} $(sha256_file "${shader}")"
    if ! grep -Fqx "${expected_source}" "${manifest}"; then
        echo "Shader source is missing or stale in ${manifest}: ${shader_file}" >&2
        exit 1
    fi

    shader_name="${shader_file%.hlsl}"
    for extension in spv msl dxil; do
        output="${output_dir}/${shader_name}.${extension}"
        if [ ! -s "${output}" ]; then
            echo "Shader artifact is missing or empty: ${output}" >&2
            exit 1
        fi
        expected_artifact="artifact=$(basename "${output}") $(sha256_file "${output}")"
        if ! grep -Fqx "${expected_artifact}" "${manifest}"; then
            echo "Shader artifact is missing or stale in ${manifest}: ${output}" >&2
            exit 1
        fi
    done
done

actual_outputs=("${output_dir}"/*.spv "${output_dir}"/*.msl "${output_dir}"/*.dxil)
expected_outputs=()
for shader in "${shaders[@]}"; do
    shader_name="$(basename "${shader%.hlsl}")"
    expected_outputs+=("${output_dir}/${shader_name}.spv" "${output_dir}/${shader_name}.msl" "${output_dir}/${shader_name}.dxil")
done

actual_sorted="$(printf '%s\n' "${actual_outputs[@]}" | sort)"
expected_sorted="$(printf '%s\n' "${expected_outputs[@]}" | sort)"
if [ "${actual_sorted}" != "${expected_sorted}" ]; then
    echo "Unexpected shader artifacts found in ${output_dir}" >&2
    diff -u <(printf '%s\n' "${expected_outputs[@]}" | sort) <(printf '%s\n' "${actual_outputs[@]}" | sort) >&2 || true
    exit 1
fi

echo "Verified ${#shaders[@]} shaders and ${expected_artifact_count} artifacts"
