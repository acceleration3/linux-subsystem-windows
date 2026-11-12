#!/usr/bin/env bash

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HERE="${ROOT}/scripts"
MAIN_TREE="${LSW_MAIN_TREE:-$(git -C "$(dirname "$0")" worktree list --porcelain | sed -n "1s/^worktree //p")}"
DEV_STACK="${LSW_RENDER_DRIVER_DIR:-${XDG_DATA_HOME:-${HOME}/.local/share}/lsw-dev/render-driver}"
GPUTEST="${HERE}/gpu-test/gpu-test.sh"
HOSTS_FILE="${LSW_GPU_TEST_HOSTS:-${HERE}/gpu-test/hosts}"
BUILD_LOCK="${XDG_RUNTIME_DIR:-/tmp}/lsw-build.lock"
REMOTE_BASE=".local/share/lsw-gpu-test"
ORIG_ARGS=("$@")

host="" stack="" filter="" adapters=all bits=64 suite=render gpus="" out="" max_hold=3600 redeploy=0 dry=0 show_queue=0 jobs=2 hold_file="" qemu_env="" env_definition="" smoke=0 bits_given=0 bits2=""

usage() {
    cat <<'EOT'
usage: scripts/verify.sh --host local|accel --stack DIR (--filter GLOB[,GLOB] | --smoke) [options]

The one way to verify a driver change on the local VM or on accel. Takes the VM's turn in a first-come first-served
queue, deploys exactly the stack in DIR (built by scripts/stack-build.sh), checks that the running QEMU maps the stack's
executor and modules, runs scripts/render-run.sh from this tree with its suites and reference tool at this tree's
revision, and leaves the results in DIR/results/<timestamp>.

  --host local|accel        the VM to use
  --stack DIR               a stack-build.sh output; its suite sha must be this tree's HEAD
  --filter GLOB[,GLOB]      check ids, e.g. 'render.d3d11.clear*,render.vk.xfer.copy*' (required unless --smoke: keep holds short)
  --smoke                   the short gate: the rows of scripts/smoke-selection.txt from both suites, run twice in one VM
                            session (pass 1 x64 and x32, pass 2 x64 only unless --bits 64|32 is given), judged for failures, QEMU crashes, host GPU kernel events, row
                            times against scripts/smoke-baseline.json (scripts/smoke_baseline.py update) and rows that
                            pass the first time and fail the second; exit 0 only when all hold
  --suite render|sanity     default render
  --adapters all|first      local: which VM-bound GPUs (default all)
  --gpus SEL[,SEL]          accel: HOST:SEL entries of render-run.sh --gpus (default every GPU)
  --bits 64|32|both         default 64 (--smoke: both, the second pass x64 only; --smoke --bits both is the same)
  --jobs N                  parallel checks per adapter (default 2)
  --out DIR                 results directory (default DIR/results/<timestamp>)
  --hold-file PATH          check ids (one per line, globs allowed) that are never run
  --max-hold SECONDS        the run is killed after this long holding the VM (default 3600)
  --qemu-env NAME=VALUE     set NAME in the executor's environment (repeatable; forces a redeploy)
  --redeploy                deploy even when the VM already runs this stack
  --queue                   show the queue of --host and exit
  --dry-run                 check the stack and print every step, touch no VM and take no ticket
EOT
}

die() { printf 'verify: %s\n' "$*" >&2; exit 1; }
say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host) host="$2"; shift ;;
        --stack) stack="$2"; shift ;;
        --filter) filter="$2"; shift ;;
        --suite) suite="$2"; shift ;;
        --adapters) adapters="$2"; shift ;;
        --gpus) gpus="$2"; shift ;;
        --bits) bits="$2"; bits_given=1; shift ;;
        --smoke) smoke=1 ;;
        --jobs) jobs="$2"; shift ;;
        --out) out="$2"; shift ;;
        --max-hold) max_hold="$2"; shift ;;
        --hold-file) hold_file="$(realpath "$2")"; shift ;;
        --redeploy) redeploy=1 ;;
        --qemu-env) qemu_env+=" $2"; redeploy=1; shift ;;
        --queue) show_queue=1 ;;
        --dry-run) dry=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "unknown option $1" ;;
    esac
    shift
done

case "${host}" in local|accel) ;; *) usage >&2; die "--host needs local or accel" ;; esac
[[ ${show_queue} -eq 1 ]] && exec python3 -B "${HERE}/vm_queue.py" status --host "${host}"
[[ -n ${stack} && -f ${stack}/manifest.json ]] || die "--stack needs a stack-build.sh output directory with manifest.json"
stack="$(cd "${stack}" && pwd)"
if [[ ${smoke} -eq 1 ]]; then
    [[ -z ${filter} ]] || die "--smoke sets the rows itself: drop --filter"
    [[ ${bits_given} -eq 1 ]] || bits=both
    bits2="${bits}"
    [[ ${bits} != both ]] || bits2=64
    filter="smoke"
fi
[[ -n ${filter} ]] || die "--filter is required: a verification holds the VM for the whole run"
[[ -n ${out} ]] || out="${stack}/results/$(date +%Y%m%d-%H%M%S)-${host}"

target="$(awk -v name="${host}" '!/^[[:space:]]*#/ && $1 == name { print $2 }' "${HOSTS_FILE}")"
[[ -n ${target} ]] || die "no host ${host} in ${HOSTS_FILE}"
ssh_opts=(-o BatchMode=yes -o ConnectTimeout=10 -o "ControlPath=${LSW_GPU_TEST_CONTROL_PATH:-/tmp/lsw-gpu-test-%C}")
on_remote() { ssh "${ssh_opts[@]}" "${target}" bash -s; }

manifest_field() { python3 -B -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "${stack}/manifest.json" "$1"; }
manifest_id() { md5sum "${stack}/manifest.json" | cut -d' ' -f1; }

check_stack() {
    local suite_sha tree_sha
    suite_sha="$(manifest_field suite_sha)"
    tree_sha="$(git -C "${ROOT}" rev-parse HEAD)"
    [[ ${suite_sha} == "${tree_sha}" || ${LSW_VERIFY_ALLOW_SUITE_MISMATCH:-0} == 1 ]] ||
        die "the stack was built with suite ${suite_sha:0:8} but this tree is ${tree_sha:0:8}: rebuild the stack here or run from the tree it was built with"
    [[ $(manifest_field hybrid) == True ]] || die "the stack's virtio-gpu-av module has no hybrid support"
    [[ $(manifest_field driver_dirty) == False ]] || printf 'verify: the stack was built from a driver tree with uncommitted changes\n' >&2
}

current_revision() {
    local rev
    rev="$(git -C "${ROOT}" rev-parse --short HEAD)"
    [[ -n $(git -C "${ROOT}" status --porcelain --untracked-files=no -- suites cmake 2>/dev/null) ]] && rev+="+dirty"
    printf '%s' "${rev}"
}

ensure_suites_built() {
    local rev b dir stale=0
    rev="$(current_revision)"
    [[ $(cat "${ROOT}/build-render-ref/REVISION" 2>/dev/null) == "${rev}" ]] || stale=1
    for b in 64 32; do
        dir="${ROOT}/build-suites"
        [[ ${b} == 32 ]] && dir="${ROOT}/build-suites32"
        [[ $(cat "${dir}/REVISION" 2>/dev/null) == "${rev}" ]] || stale=1
    done
    [[ ${stale} -eq 0 ]] && { printf 'verify: suites and lsw-render-ref are built at %s\n' "${rev}"; return 0; }
    if [[ ${dry} -eq 1 ]]; then
        printf '[dry-run] build lsw-suites (both bitnesses) and lsw-render-ref at %s under the build lock\n' "${rev}"
        return 0
    fi
    local av="${ROOT}/external/accel-virt"
    [[ -f ${av}/third_party/khronos/include/CL/cl.h || -d ${av}/third_party/khronos ]] || av="${MAIN_TREE}/external/accel-virt"
    say "build suites and lsw-render-ref at ${rev}"
    flock -w 7200 "${BUILD_LOCK}" systemd-run --user --scope -q -p CPUQuota=400% -p MemoryHigh=6G nice -n 19 ionice -c3 \
        env CMAKE_BUILD_PARALLEL_LEVEL=4 LSW_SUITES_KHRONOS_DIR="${av}/third_party/khronos" LSW_SUITES_BITS=both bash -c \
        '"$1/scripts/build-suites.sh" && "$1/scripts/build-render-ref.sh"' _ "${ROOT}" || die "the suite build failed"
}

mapped_report() {
    if [[ ${host} == local ]]; then
        python3 -B "${HERE}/stack_manifest.py" report
    else
        rsync -a -e "ssh ${ssh_opts[*]}" "${HERE}/stack_manifest.py" "${target}:${REMOTE_BASE}/stack_manifest.py" || return 1
        on_remote <<< "python3 -B ~/${REMOTE_BASE}/stack_manifest.py report"
    fi
}

check_running() {
    local report
    report="$(mktemp)"
    if ! mapped_report > "${report}" 2>/dev/null; then
        rm -f "${report}"
        return 2
    fi
    python3 -B "${HERE}/stack_manifest.py" check --stack "${stack}" --mapped "${report}" "$@"
    local rc=$?
    rm -f "${report}"
    return ${rc}
}


deployed_id() {
    if [[ ${host} == local ]]; then cat "${DEV_STACK}/stack.deployed" 2>/dev/null; else on_remote <<< "cat ~/${REMOTE_BASE}/stack.deployed 2>/dev/null"; fi
}

record_deployed() {
    if [[ ${host} == local ]]; then manifest_id > "${DEV_STACK}/stack.deployed"; else on_remote <<< "echo $(manifest_id) > ~/${REMOTE_BASE}/stack.deployed"; fi
}

forget_deployed() {
    if [[ ${host} == local ]]; then rm -f "${DEV_STACK}/stack.deployed"; else on_remote <<< "rm -f ~/${REMOTE_BASE}/stack.deployed"; fi
}

deploy_local() {
    say "local: install the stack into ${DEV_STACK} and restart the guest"
    rsync -a "${stack}/libav_executor.so" "${DEV_STACK}/libav_executor.so" || return 1
    rsync -a --delete "${stack}/av-drivers/" "${DEV_STACK}/av-drivers/" || return 1
    rsync -a --delete "${stack}/d3dlib/" "${DEV_STACK}/d3dlib/" || return 1
    rsync -a --delete "${stack}/qemu-modules/" "${DEV_STACK}/qemu-modules/" || return 1
    env -u LSW_AV_BUILD_EXEC -u LSW_AV_BUILD_DRIVERS "${DEV_STACK}/redeploy.sh" || return 1
    [[ -n ${qemu_env} ]] || return 0
    say "local: define the domain with${qemu_env} for this verification and restart the guest"
    python3 -B "${HERE}/domain_env.py" "${DEV_STACK}/state/lsw.new.xml" "${env_definition}" ${qemu_env} || return 1
    virsh -c qemu:///session define "${env_definition}" > /dev/null || return 1
    virsh -c qemu:///session destroy lsw > /dev/null 2>&1
    "${HERE}/render-vm.sh" up
}

restore_definition() {
    [[ -n ${env_definition} ]] || return 0
    if [[ -s ${env_definition} ]]; then
        say "local: define the domain from ${DEV_STACK}/state/lsw.new.xml again"
        virsh -c qemu:///session define "${DEV_STACK}/state/lsw.new.xml" > /dev/null
    fi
    rm -f "${env_definition}"
    env_definition=""
}

deploy_accel() {
    say "accel: deploy-render and stack-apply"
    rsync -a "${stack}/stage/" "${ROOT}/" || return 1
    LSW_RENDER_STACK="${stack}" "${GPUTEST}" deploy-render accel || return 1
    LSW_STACK_QEMU_ENV="${qemu_env}" "${GPUTEST}" stack-apply accel
}

deploy_if_needed() {
    local rc
    if [[ ${redeploy} -eq 0 && $(deployed_id) == "$(manifest_id)" ]]; then
        check_running --package
        rc=$?
        if [[ ${rc} -eq 0 ]]; then
            printf 'verify: the VM already runs this stack and its guest has the stack driver package\n'
            return 0
        fi
    fi
    forget_deployed
    "deploy_${host}" || return 1
    check_running || return $?
    [[ -n ${qemu_env} ]] || record_deployed
}

smoke_bits() { [[ $1 -eq 1 ]] && echo "${bits}" || echo "${bits2}"; }

smoke_rows() {
    local pass=$1 which=$2 rows_filter args
    rows_filter="$(python3 -B "${HERE}/smoke_verdict.py" filter --suite "${which}")"
    args=(--parallel --no-build --hosts "${host}" --bits "$(smoke_bits "${pass}")" --filter "${rows_filter}" --suite "${which}" --jobs "${jobs}" --out "${out}/pass${pass}")
    [[ ${pass} -eq 2 && ${which} == render ]] && args+=(--reuse-refs "${out}/pass1")
    [[ ${host} == local ]] && args+=(--adapters "${adapters}")
    [[ -n ${gpus} ]] && args+=(--gpus "${gpus}")
    [[ -n ${hold_file} ]] && args+=(--hold-file "${hold_file}")
    say "smoke pass ${pass}: ${which} rows"
    "${HERE}/render-run.sh" "${args[@]}"
}

kernel_log_since() {
    local since=$1
    if [[ ${host} == local ]]; then
        journalctl -k --no-pager --since "@${since}" 2>&1
    else
        on_remote <<< "journalctl -k --no-pager --since @${since} 2>&1 || dmesg 2>&1"
    fi
}

smoke_run() {
    local since pass which
    since="$(date +%s)"
    for pass in 1 2; do
        for which in render sanity; do
            smoke_rows "${pass}" "${which}"
        done
    done
    kernel_log_since "${since}" > "${out}/smoke-kernel.log"
    python3 -B "${HERE}/smoke_verdict.py" verdict --pass "${out}/pass1" --pass "${out}/pass2" --kernel-log "${out}/smoke-kernel.log" --pass2-bits "${bits2}" --summary "${out}/smoke-summary.txt"
}

run_inner() {
    local lockfd rc status
    if [[ ${host} == local ]]; then
        exec {lockfd}> "${XDG_RUNTIME_DIR:-/tmp}/lsw-render.lock"
        flock -w 900 "${lockfd}" || die "lsw-render.lock is held by something outside the queue"
        export LSW_RENDER_HEAVY_LOCK="${XDG_RUNTIME_DIR:-/tmp}/lsw-render-inner.lock"
        if [[ -n ${qemu_env} ]]; then
            env_definition="$(mktemp -p "${XDG_RUNTIME_DIR:-/tmp}" lsw-env-XXXXXX.xml)"
            trap restore_definition EXIT
            trap 'exit 130' INT
            trap 'exit 143' TERM
        fi
    else
        "${GPUTEST}" master accel >/dev/null 2>&1 || { "${GPUTEST}" master accel >&2; die "no ssh master for accel"; }
        coproc ACCEL_LOCK { ssh "${ssh_opts[@]}" "${target}" bash -s; }
        printf 'exec 9>/tmp/lsw-accel-vm.lock; flock -w 900 9 || exit 1; echo locked; cat > /dev/null\n' >&"${ACCEL_LOCK[1]}"
        local line=""
        read -r -t 1000 line <&"${ACCEL_LOCK[0]}"
        [[ ${line} == locked ]] || die "could not take /tmp/lsw-accel-vm.lock on accel"
        export LSW_RENDER_REMOTE_LOCK_HELD=1
    fi
    mkdir -p "${out}"
    cp "${stack}/manifest.json" "${out}/stack-manifest.json"
    deploy_if_needed | tee "${out}/deploy.log"
    rc=${PIPESTATUS[0]}
    if [[ ${rc} -ne 0 ]]; then
        printf 'verify: the VM does not run the stack (exit %s); no rows were run\n' "${rc}" >&2
        status=3
    elif [[ ${smoke} -eq 1 ]]; then
        smoke_run
        status=$?
    else
        local args=(--parallel --no-build --hosts "${host}" --bits "${bits}" --filter "${filter}" --suite "${suite}" --jobs "${jobs}" --out "${out}")
        [[ ${host} == local ]] && args+=(--adapters "${adapters}")
        [[ -n ${gpus} ]] && args+=(--gpus "${gpus}")
        [[ -n ${hold_file} ]] && args+=(--hold-file "${hold_file}")
        "${HERE}/render-run.sh" "${args[@]}"
        status=$?
    fi
    restore_definition
    [[ -n ${lockfd:-} ]] && eval "exec ${lockfd}>&-"
    [[ -n ${ACCEL_LOCK_PID:-} ]] && { exec {ACCEL_LOCK[1]}>&-; wait "${ACCEL_LOCK_PID}" 2>/dev/null; }
    return "${status}"
}

if [[ ${LSW_VERIFY_INNER:-0} == 1 ]]; then
    run_inner
    exit $?
fi

check_stack
ensure_suites_built
if [[ ${dry} -eq 1 ]]; then
    printf '[dry-run] queue ticket on %s in %s\n' "${host}" "${LSW_VERIFY_QUEUE:-${XDG_RUNTIME_DIR:-/tmp}/lsw-verify-queue}"
    printf '[dry-run] hold %s, killed after %ss\n' "$([[ ${host} == local ]] && echo 'lsw-render.lock' || echo '/tmp/lsw-accel-vm.lock on accel')" "${max_hold}"
    printf '[dry-run] deploy stack %s (suite %s, driver %s) unless the VM already runs it and its guest has its driver package\n' "$(manifest_id)" "$(manifest_field suite_sha | cut -c1-8)" "$(manifest_field driver_sha | cut -c1-8)"
    printf '[dry-run] check the md5 of the executor and both modules the QEMU maps against the manifest\n'
    if [[ ${smoke} -eq 1 ]]; then
        for pass in 1 2; do
            for which in render sanity; do
                printf '[dry-run] smoke pass %s: render-run.sh --parallel --no-build --hosts %s --bits %s --suite %s --filter <%s rows of scripts/smoke-selection.txt>%s%s -> %s/pass%s\n' "${pass}" "${host}" "$(smoke_bits "${pass}")" "${which}" "${which}" "$([[ ${host} == local ]] && echo " --adapters ${adapters}")" "$([[ ${pass} -eq 2 && ${which} == render ]] && echo " --reuse-refs ${out}/pass1")" "${out}" "${pass}"
            done
        done
        printf '[dry-run] collect the kernel log since the start, then smoke_verdict.py: failures, QEMU crashes, kernel events, row times against scripts/smoke-baseline.json, rows degraded in pass 2 (x%s)\n' "${bits2}"
    else
        printf '[dry-run] render-run.sh --parallel --no-build --hosts %s --bits %s --filter %s --suite %s%s -> %s\n' "${host}" "${bits}" "${filter}" "${suite}" "$([[ ${host} == local ]] && echo " --adapters ${adapters}")" "${out}"
    fi
    exit 0
fi
exec python3 -B "${HERE}/vm_queue.py" hold --host "${host}" --who "${stack##*/} ${filter}" -- \
    timeout --kill-after=60 "${max_hold}" env LSW_VERIFY_INNER=1 "$0" "${ORIG_ARGS[@]}"
