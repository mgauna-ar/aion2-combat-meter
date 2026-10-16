#!/bin/sh
# The meter is passive (AGENTS.md, "Passive-only"): it never opens the game
# process, reads or writes its memory, injects, hooks, or sends packets or input.
# This fails the build when one of those APIs shows up in our own code. Vendored
# libraries (src/third_party) and this script are left out; docs may name them.
#
# Process enumeration through a Toolhelp snapshot and the TCP table are allowed:
# neither opens a handle to another process.

set -u
cd "$(dirname "$0")/.." || exit 2

PATTERN='OpenProcess|ReadProcessMemory|WriteProcessMemory|VirtualAllocEx|VirtualProtectEx|CreateRemoteThread|NtCreateThreadEx|QueueUserAPC|SetWindowsHookEx|SendInput|keybd_event|mouse_event|PostThreadMessage|pcap_sendpacket|pcap_inject|pcap_sendqueue|WinDivert|DebugActiveProcess|MiniDumpWriteDump'

matches=$(grep -rnE "$PATTERN" include src tools tests \
    --include='*.cpp' --include='*.hpp' --include='*.h' --include='*.c' \
    2>/dev/null | grep -v '^src/third_party/')

if [ -n "$matches" ]; then
    echo "Forbidden API check: FAILED. The meter must stay passive (AGENTS.md):"
    echo "$matches"
    exit 1
fi
echo "Forbidden API check: OK"
