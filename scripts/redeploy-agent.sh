#!/usr/bin/env bash
# Hot-swap the running guest agent inside the LSW VM with a fresh
# cross-build of lsw-guest-agent.exe, and the window mirror's guest half
# (remotewin-guest.exe) with it. No VM reboot, no manual scp/console
# juggling.
#
# Mechanism: forwards to the running manager's MCP server
# (http://127.0.0.1:5105/). The manager:
#   1. Reads the local agent binary, and remotewin-guest.exe when there
#      is one ($LSW_REMOTEWIN_GUEST or build-agent/), into memory.
#   2. Serves both on a free localhost port, each under its own name.
#   3. Sends a "swap script" to the agent via run_script RPC (it runs
#      as SYSTEM, outliving the agent it stops); the script curls the
#      new binaries from 10.0.2.2:<port>, renames the guest's
#      remotewin-guest.exe aside (Windows renames a running image but
#      will not replace it; the aside copy is deleted by a later swap
#      once nothing runs it) and moves the new one in, stops the
#      ``lsw-agent`` service, ends anything still running the old agent
#      (the session helpers), moves the agent into C:\ProgramData\LSW\,
#      and starts the service again.
#   4. Polls system_info until ``agent_version`` changes, confirming
#      the new binary is live (the new service opens :5102 once its
#      session helper has confirmed the desktop, a few seconds in), then
#      checks the guest's remotewin-guest.exe is the size of the one
#      served. Mirrors already running keep the old guest half; the next
#      application launched gets the new one.
#
# The script's own log is C:\ProgramData\LSW\lsw-swap.log in the guest.
#
# Usage:
#   scripts/redeploy-agent.sh             # build, then swap
#   scripts/redeploy-agent.sh --no-build  # use existing build-agent/ output
#
# Requires: a running lsw-manager (with MCP enabled on :5105) and a
# running VM with the current agent reachable. The manager's own build
# decides what it serves: one older than this checkout serves the agent
# alone.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MCP_URL="${LSW_MCP_URL:-http://127.0.0.1:5105/}"
DO_BUILD=1

for arg in "$@"; do
    case "$arg" in
        --no-build) DO_BUILD=0 ;;
        -h|--help)
            sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'
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

# Forward to the manager. The manager owns the deploy state machine
# (HTTP server, RPC, version polling); we're just calling tools/call.
echo ">>> Calling redeploy_agent on MCP at $MCP_URL"
REPLY=$(curl -sS --fail -X POST "$MCP_URL" \
    -H "Content-Type: application/json" \
    --data '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"redeploy_agent","arguments":{}}}')

# Pull the human-readable text out of the MCP result envelope.
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
