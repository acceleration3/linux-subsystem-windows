#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MCP_URL="${LSW_MCP_URL:-http://127.0.0.1:5105/}"
DO_BUILD=1

usage() {
    cat <<'EOF'
usage: scripts/redeploy-agent.sh [--no-build]

Replaces the guest agent and remotewin-guest.exe in the running VM with the
current cross-build, through the manager's MCP server (127.0.0.1:5105).

  --no-build  use the existing build-agent/ output
EOF
}


for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "redeploy-agent: unknown argument '$arg'" >&2
            exit 2
            ;;
    esac
done

if [[ "$DO_BUILD" -eq 1 ]]; then
    echo ">>> Cross-building agent"
    "${ROOT}/scripts/build-agent.sh"
fi

EXE="${LSW_AGENT_EXE:-${ROOT}/build-agent/src/agent/lsw-guest-agent.exe}"
if [[ ! -f "$EXE" ]]; then
    echo "redeploy-agent: $EXE not found; run scripts/build-agent.sh first" >&2
    exit 1
fi
SIZE=$(stat -c%s "$EXE")
echo ">>> Will deploy $EXE ($SIZE bytes)"
MIRROR="${LSW_REMOTEWIN_GUEST:-${ROOT}/build-agent/remotewin/source/guest/remotewin-guest.exe}"
if [[ -f "$MIRROR" ]]; then
    echo ">>> and $MIRROR ($(stat -c%s "$MIRROR") bytes)"
else
    echo ">>> no $MIRROR: the guest keeps its remotewin-guest.exe"
fi

echo ">>> Calling redeploy_agent on MCP at $MCP_URL"
REPLY=$(curl -sS --fail -X POST "$MCP_URL" \
    -H "Content-Type: application/json" \
    --data '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"redeploy_agent","arguments":{}}}')

TEXT=$(printf '%s\n' "$REPLY" | python3 -c '
import json, sys
try:
    obj = json.load(sys.stdin)
except json.JSONDecodeError as e:
    print("redeploy-agent: malformed MCP reply:", e, file=sys.stderr)
    sys.exit(1)
if "error" in obj:
    print("redeploy-agent: MCP error:", obj["error"], file=sys.stderr)
    sys.exit(1)
res = obj.get("result", {})
is_err = res.get("isError", False)
parts = []
for c in res.get("content", []):
    if c.get("type") == "text":
        parts.append(c.get("text", ""))
print("\n".join(parts))
sys.exit(1 if is_err else 0)
') && RC=0 || RC=$?
echo "$TEXT"
exit $RC
