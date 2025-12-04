#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GENERATOR="${ROOT_DIR}/generator/pattern"
CONFIG_PATH="${1:-${ROOT_DIR}/config/pattern_targets.txt}"

if [[ ! -x "${GENERATOR}" ]]; then
    echo "error: generator binary not found at ${GENERATOR}. Run 'make' first." >&2
    exit 1
fi

if [[ ! -f "${CONFIG_PATH}" ]]; then
    echo "error: config file not found at ${CONFIG_PATH}" >&2
    exit 1
fi

while IFS= read -r line || [[ -n "${line}" ]]; do
    # Strip comments and whitespace.
    line="${line%%#*}"
    line="$(echo "${line}" | xargs)"
    if [[ -z "${line}" ]]; then
        continue
    fi

    read -r circuit count seed <<<"${line}"
    if [[ -z "${circuit}" || -z "${count}" ]]; then
        echo "warning: skipping malformed line '${line}'" >&2
        continue
    fi
    seed="${seed:-42}"
    echo "Generating ${count} patterns for ${circuit} (seed ${seed})..."
    "${GENERATOR}" "${circuit}" "${count}" "${seed}"
done < "${CONFIG_PATH}"

echo "All patterns regenerated from ${CONFIG_PATH}"
