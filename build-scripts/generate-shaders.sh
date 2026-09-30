#!/usr/bin/env bash
set -euo pipefail

shadercross="${SHADERCROSS:-shadercross}"
source_dir="${1:-src/shaders}"
output_dir="${2:-data/shaders}"
manifest="${output_dir}/shader-artifacts.manifest"
manifest_tmp="${manifest}.tmp.$$"

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 256 "$1" | cut -d ' ' -f 1
    fi
}

if ! command -v "${shadercross}" >/dev/null 2>&1; then
    echo "shadercross executable not found: ${shadercross}" >&2
    exit 1
fi

if [ ! -d "${source_dir}" ]; then
    echo "Shader source directory not found: ${source_dir}" >&2
    exit 1
fi

mkdir -p "${output_dir}"
find "${output_dir}" -maxdepth 1 \( -name '*.spv' -o -name '*.msl' -o -name '*.dxil' -o -name 'shader-artifacts.manifest' \) -delete

shopt -s nullglob
shaders=("${source_dir}"/*.hlsl)
if [ "${#shaders[@]}" -eq 0 ]; then
    echo "No HLSL shaders found in ${source_dir}" >&2
    exit 1
fi

trap 'rm -f "${manifest_tmp}"' EXIT
for shader in "${shaders[@]}"; do
    shader_file="$(basename "${shader}")"
    shader_name="${shader_file%.hlsl}"
    stage="compute"
    if [[ "${shader_name}" == *_vertex ]]; then
        stage="vertex"
    elif [[ "${shader_name}" == *_fragment ]]; then
        stage="fragment"
    fi

    "${shadercross}" "${shader}" -s hlsl -d spirv -t "${stage}" -o "${output_dir}/${shader_name}.spv"
    "${shadercross}" "${shader}" -s hlsl -d msl -t "${stage}" -o "${output_dir}/${shader_name}.msl"
    "${shadercross}" "${shader}" -s hlsl -d dxil -t "${stage}" -o "${output_dir}/${shader_name}.dxil"
done

{
    echo "shader-artifacts-version=1"
    echo "targets=spv msl dxil"
    for shader in "${shaders[@]}"; do
        shader_file="$(basename "${shader}")"
        printf 'source=%s %s\n' "${shader_file}" "$(sha256_file "${shader}")"
    done
    for shader in "${shaders[@]}"; do
        shader_name="$(basename "${shader%.hlsl}")"
        for extension in spv msl dxil; do
            output="${output_dir}/${shader_name}.${extension}"
            printf 'artifact=%s %s\n' "$(basename "${output}")" "$(sha256_file "${output}")"
        done
    done
} > "${manifest_tmp}"
mv "${manifest_tmp}" "${manifest}"
trap - EXIT
