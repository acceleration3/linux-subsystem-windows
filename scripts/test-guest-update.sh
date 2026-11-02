#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MCP_URL="${LSW_MCP_URL:-http://127.0.0.1:5105/}"
HARNESS="${ROOT}/src/guest/update/tests/lsw-guest-update.tests.ps1"
SCRIPT="${ROOT}/src/guest/update/lsw-guest-update.ps1"
GUEST_DIR='C:\ProgramData\LSW\update-tests'
GUEST_LOG='C:\ProgramData\LSW\update-tests.log'
ONLY=""
KEEP=0
TIMEOUT_MIN=60

usage() {
    cat <<'EOF'
usage: scripts/test-guest-update.sh [--only LETTERS] [--keep] [--timeout MINUTES]

Runs the in-VM scenario tests of lsw-guest-update.ps1 against the running
guest through the manager's MCP server (127.0.0.1:5105) and prints their log.
Exits 0 when every check passed, 1 on a failed check, 2 when the driver
itself failed.

  --only LETTERS     run only these scenarios
  --keep             leave the uploaded files and the log in the guest
  --timeout MINUTES  how long to wait for the summary line (default 60)
EOF
}


while [[ $# -gt 0 ]]; do
    case "$1" in
        --only) ONLY="$2"; shift 2 ;;
        --keep) KEEP=1; shift ;;
        --timeout) TIMEOUT_MIN="$2"; shift 2 ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "test-guest-update: unknown argument '$1'" >&2
            exit 2
            ;;
    esac
done

for f in "$HARNESS" "$SCRIPT"; do
    [[ -f "$f" ]] || { echo "test-guest-update: $f not found" >&2; exit 2; }
done

mcp() {
    local reply
    reply=$(curl -sS --fail -X POST "$MCP_URL" -H "Content-Type: application/json" \
        --data "$(python3 -c 'import json,sys; print(json.dumps({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":sys.argv[1],"arguments":json.loads(sys.argv[2])}}))' "$1" "$2")") || return 1
    printf '%s\n' "$reply" | python3 -c '
import json, sys
obj = json.load(sys.stdin)
if "error" in obj:
    print(obj["error"], file=sys.stderr); sys.exit(1)
res = obj.get("result", {})
print("\n".join(c.get("text", "") for c in res.get("content", []) if c.get("type") == "text"))
sys.exit(1 if res.get("isError", False) else 0)
'
}

guest_cmd() {
    local line="$1" wait="${2:-10}" sid out="" chunk i done_seen=0
    sid=$(mcp console_open '{"session":0}' 2>/dev/null | sed -n 's/^session_id=\([^ ]*\).*/\1/p') || return 1
    [[ -n "$sid" ]] || return 1
    sleep 1
    mcp console_input "$(python3 -c 'import json,sys; print(json.dumps({"session_id":sys.argv[1],"data":sys.argv[2]+"\r\necho __DONE_%errorlevel%__\r\n"}))' "$sid" "$line")" >/dev/null || return 1
    for ((i = 0; i < wait * 2; i++)); do
        sleep 0.5
        chunk=$(mcp console_read "{\"session_id\":\"$sid\"}" 2>/dev/null) || true
        out+="$chunk"
        if grep -qE '__DONE_[0-9]+__' <<<"$out"; then done_seen=1; break; fi
    done
    mcp console_close "{\"session_id\":\"$sid\"}" >/dev/null 2>&1 || true
    printf '%s\n' "$out"
    [[ "$done_seen" -eq 1 ]]
}

upload() {
    local file="$1" dest="$2" name stored want got
    name="$(basename "$file")"
    stored=$(python3 - "$file" "$MCP_URL" <<'PY'
import base64, json, os, sys, urllib.request
path, url = sys.argv[1], sys.argv[2]
def rpc(params):
    body = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
            "params": {"name": "rpc", "arguments": {"cmd": "upload_file", "params": params}}}
    reply = json.load(urllib.request.urlopen(urllib.request.Request(
        url, data=json.dumps(body).encode(), headers={"Content-Type": "application/json"}), timeout=120))
    res = reply["result"]
    text = "".join(c.get("text", "") for c in res["content"])
    if res.get("isError"):
        raise SystemExit("upload_file: " + text)
    return json.loads(text)
data = open(path, "rb").read()
uid = rpc({"action": "begin", "name": os.path.basename(path), "size": len(data)})["upload_id"]
offset = 0
while True:
    chunk = data[offset:offset + 150000]
    if not chunk and offset:
        break
    rpc({"action": "chunk", "upload_id": uid, "offset": offset, "data": base64.b64encode(chunk).decode()})
    offset += len(chunk)
    if offset >= len(data):
        break
print(rpc({"action": "finish", "upload_id": uid})["path"])
PY
    ) || return 1
    guest_cmd "md \"$dest\" 2>nul & copy /y \"$stored\" \"$dest\\$name\" >nul & rd /s /q \"$(dirname_win "$stored")\"" 15 >/dev/null || return 1
    want=$(sha256sum "$file" | cut -d' ' -f1)
    got=$(mcp rpc "$(python3 -c 'import json,sys; print(json.dumps({"cmd":"file_hashes","params":{"paths":[sys.argv[1]]}}))' "$dest\\$name")" \
        | python3 -c 'import json,sys; print(next(iter(json.load(sys.stdin)["hashes"].values())) or "")')
    if [[ "$got" != "$want" ]]; then
        echo "test-guest-update: $name reached the guest as $got, not $want" >&2
        return 1
    fi
}

dirname_win() { printf '%s' "${1%\\*}"; }

echo ">>> Uploading the harness and the script under test to $GUEST_DIR"
mcp system_info '{}' >/dev/null || { echo "test-guest-update: the manager or the agent does not answer" >&2; exit 2; }
guest_cmd "if exist \"$GUEST_LOG\" del /f /q \"$GUEST_LOG\"" 10 >/dev/null || { echo "test-guest-update: no console in the guest" >&2; exit 2; }
upload "$HARNESS" "$GUEST_DIR" || exit 2
upload "$SCRIPT" "$GUEST_DIR" || exit 2

echo ">>> Starting the harness as the one-shot task lsw-update-tests (SYSTEM)"
RUN="powershell -NoProfile -ExecutionPolicy Bypass -File $GUEST_DIR\\lsw-guest-update.tests.ps1 -ScriptUnderTest $GUEST_DIR\\lsw-guest-update.ps1 -LogFile $GUEST_LOG"
[[ -z "$ONLY" ]] || RUN+=" -Only $ONLY"
guest_cmd "schtasks /Delete /TN lsw-update-tests /F >nul 2>&1 & schtasks /Create /TN lsw-update-tests /SC ONCE /ST 00:00 /RU SYSTEM /RL HIGHEST /F /TR \"$RUN\" & schtasks /Run /TN lsw-update-tests" 15 || exit 2

echo ">>> Waiting for the summary (up to $TIMEOUT_MIN min; the agent restarts meanwhile)"
SUMMARY=""
deadline=$((SECONDS + TIMEOUT_MIN * 60))
while ((SECONDS < deadline)); do
    sleep 20
    if out=$(guest_cmd "findstr /c:\"SUMMARY:\" \"$GUEST_LOG\"" 10 2>/dev/null); then
        SUMMARY=$(grep -oE 'SUMMARY: [0-9]+ passed, [0-9]+ failed' <<<"$out" | tail -n1 || true)
        [[ -z "$SUMMARY" ]] || break
    fi
done

LOG=""
for _ in 1 2 3 4 5 6; do
    if LOG=$(guest_cmd "type \"$GUEST_LOG\"" 20 2>/dev/null) && [[ -n "$LOG" ]]; then break; fi
    sleep 10
done
echo
echo "---- $GUEST_LOG ----"
tr -d '\r' <<<"$LOG" | sed 's/^C:\\Windows\\System32>//' | grep -E '^[0-9]{2}:[0-9]{2}:[0-9]{2} |^===|^SUMMARY'
echo "--------------------"

if [[ "$KEEP" -eq 0 ]]; then
    guest_cmd "schtasks /Delete /TN lsw-update-tests /F >nul 2>&1 & rd /s /q \"$GUEST_DIR\" & del /f /q \"$GUEST_LOG\" & rd /s /q C:\\ProgramData\\LSW\\uploads" 15 >/dev/null 2>&1 || true
fi

if [[ -z "$SUMMARY" ]]; then
    echo "test-guest-update: no summary line after $TIMEOUT_MIN min" >&2
    exit 2
fi
echo "$SUMMARY"
FAILED=$(sed -E 's/.* ([0-9]+) failed.*/\1/' <<<"$SUMMARY")
[[ "$FAILED" -eq 0 ]]
