#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <program> [circuit...]" >&2
    echo "  Example: $0 ./bin/main c17 c432" >&2
    echo "  (omit circuits to run every testcase)" >&2
    exit 1
fi

PROGRAM="$1"
shift

if [[ ! -x "${PROGRAM}" ]]; then
    echo "error: program '${PROGRAM}' is not executable" >&2
    exit 1
fi

if [[ $# -eq 0 ]]; then
    shopt -s nullglob
    circuits=()
    for path in testcases/*.in; do
        circuits+=("$(basename "${path}" .in)")
    done
    shopt -u nullglob
    if [[ ${#circuits[@]} -eq 0 ]]; then
        echo "error: no testcases/*.in files found" >&2
        exit 1
    fi
else
    circuits=("$@")
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
table_rows=()
detail_lines=()

print_table() {
    local header_c="Circuit"
    local header_r="Result"
    local header_rt="Real(s)"
    local header_ct="Compute(s)"
    local max_c=${#header_c}
    local max_r=${#header_r}
    local max_rt=${#header_rt}
    local max_ct=${#header_ct}
    local c r rt ct
    for row in "${table_rows[@]}"; do
        IFS='|' read -r c r rt ct <<<"${row}"
        [[ ${#c} -gt ${max_c} ]] && max_c=${#c}
        [[ ${#r} -gt ${max_r} ]] && max_r=${#r}
        [[ ${#rt} -gt ${max_rt} ]] && max_rt=${#rt}
        [[ ${#ct} -gt ${max_ct} ]] && max_ct=${#ct}
    done

    local pad_c=$((max_c + 2))
    local pad_r=$((max_r + 2))
    local pad_rt=$((max_rt + 2))
    local pad_ct=$((max_ct + 2))

    repeat_char() {
        local count="$1"
        local char="$2"
        local result
        printf -v result '%*s' "${count}" ''
        result=${result// /${char}}
        printf '%s' "${result}"
    }

    print_border() {
        printf "+"
        repeat_char "${pad_c}" "-"
        printf "+"
        repeat_char "${pad_r}" "-"
        printf "+"
        repeat_char "${pad_rt}" "-"
        printf "+"
        repeat_char "${pad_ct}" "-"
        printf "+\n"
    }

    print_border
    printf "| %-*s | %-*s | %-*s | %-*s |\n" "${max_c}" "${header_c}" "${max_r}" "${header_r}" "${max_rt}" "${header_rt}" "${max_ct}" "${header_ct}"
    print_border
    for row in "${table_rows[@]}"; do
        IFS='|' read -r c r rt ct <<<"${row}"
        printf "| %-*s | %-*s | %-*s | %-*s |\n" "${max_c}" "${c}" "${max_r}" "${r}" "${max_rt}" "${rt}" "${max_ct}" "${ct}"
    done
    print_border
}

render_table() {
    if [[ ${#table_rows[@]} -gt 0 ]]; then
        echo
        print_table
    fi
}

for circuit in "${circuits[@]}"; do
    input_path="testcases/${circuit}.in"
    ans_sha_path="testcases/${circuit}.ans.sha"
    if [[ ! -f "${input_path}" || ! -f "${ans_sha_path}" ]]; then
        table_rows+=("${circuit}|MISSING|N/A|N/A")
        detail_lines+=("${circuit}: missing input or answer file.")
        status=1
        render_table
        continue
    fi

    temp_out="$(mktemp)"
    time_log="$(mktemp)"

    set +e
    { "${TIME_CMD[@]}" "${PROGRAM}" "${circuit}" "${temp_out}"; } 2> "${time_log}"
    prog_status=$?
    set -e

    real_time="$(grep '^real' "${time_log}" | awk '{print $2}' || echo 'N/A')"
    compute_time="$(grep '^compute_time_s' "${time_log}" | awk '{print $2}' | tail -n 1)"
    [[ -z "${compute_time}" ]] && compute_time="N/A"
    rm -f "${time_log}"

    if [[ ${prog_status} -ne 0 ]]; then
        rm -f "${temp_out}"
        table_rows+=("${circuit}|FAILED|${real_time}|${compute_time}")
        detail_lines+=("${circuit}: program exited with status ${prog_status}.")
        status=1
        render_table
        continue
    fi

    digest="$("${HASH_CMD[@]}" "${temp_out}" | awk '{print $1}')"
    expected="$(tr -d '\r\n' < "${ans_sha_path}")"
    if [[ "${digest}" == "${expected}" ]]; then
        table_rows+=("${circuit}|OK|${real_time}|${compute_time}")
    else
        table_rows+=("${circuit}|MISMATCH|${real_time}|${compute_time}")
        detail_lines+=("${circuit}: expected ${expected}, actual ${digest}.")
        status=1
    fi

    rm -f "${temp_out}"
    render_table
done

if [[ ${#detail_lines[@]} -gt 0 ]]; then
    echo
    echo "Details:"
    for line in "${detail_lines[@]}"; do
        echo "  - ${line}"
    done
fi

exit ${status}
