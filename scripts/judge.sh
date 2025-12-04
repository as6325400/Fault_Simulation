#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <program> <circuit...>" >&2
    echo "  Example: $0 ./bin/main c17 c432" >&2
    exit 1
fi

PROGRAM="$1"
shift

if [[ ! -x "${PROGRAM}" ]]; then
    echo "error: program '${PROGRAM}' is not executable" >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    HASH_CMD=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
    HASH_CMD=(shasum -a 256)
else
    echo "error: need sha256sum or shasum -a 256 available" >&2
    exit 1
fi

if command -v /usr/bin/time >/dev/null 2>&1; then
    TIME_CMD=(/usr/bin/time -p)
else
    TIME_CMD=(time -p)
fi

status=0
for circuit in "$@"; do
    input_path="testcases/${circuit}.in"
    ans_sha_path="testcases/${circuit}.ans.sha"
    if [[ ! -f "${input_path}" || ! -f "${ans_sha_path}" ]]; then
        echo "${circuit}: missing input or answer in testcases/" >&2
        status=1
        continue
    fi

    temp_out="$(mktemp)"
    time_log="$(mktemp)"
    echo "Running ${circuit}..."

    set +e
    { "${TIME_CMD[@]}" "${PROGRAM}" "${circuit}" "${temp_out}"; } 2> "${time_log}"
    prog_status=$?
    set -e

    real_time="$(grep '^real' "${time_log}" | awk '{print $2}' || echo 'N/A')"
    rm -f "${time_log}"

    if [[ ${prog_status} -ne 0 ]]; then
        echo "  FAILED (exit ${prog_status})"
        rm -f "${temp_out}"
        status=1
        continue
    fi

    digest="$("${HASH_CMD[@]}" "${temp_out}" | awk '{print $1}')"
    expected="$(tr -d '\r\n' < "${ans_sha_path}")"
    if [[ "${digest}" == "${expected}" ]]; then
        echo "  OK  (sha match, real ${real_time}s)"
    else
        echo "  MISMATCH (real ${real_time}s)"
        echo "    expected: ${expected}"
        echo "    actual  : ${digest}"
        status=1
    fi

    rm -f "${temp_out}"
done

exit ${status}
