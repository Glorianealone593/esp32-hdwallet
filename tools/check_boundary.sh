#!/usr/bin/env bash
# DibaVault — security boundary check. dibachain.
#
# Fails the build if the UNTRUSTED layers (connectivity, console-remote paths)
# include the secret-holding headers directly. They must go through vault_ipc.h.
# The console is LOCAL-origin and is allowed vault_ipc.h too, but never the raw
# keystore/signer internals.
set -euo pipefail
cd "$(dirname "$0")/.."

FORBIDDEN='keystore\.h|signer\.h'
UNTRUSTED_DIRS='components/connectivity components/dv_console'

fail=0
for d in $UNTRUSTED_DIRS; do
    if grep -REn "#include\s+\"($FORBIDDEN)\"" "$d" 2>/dev/null; then
        echo "BOUNDARY VIOLATION: $d must not include keystore.h/signer.h directly."
        fail=1
    fi
done

# The vault must never hand a private key back across the IPC. Flag any response
# field that looks like it exposes private material.
if grep -REn 'private_key|secret_key|seed\[' components/secure_core/include/vault_ipc.h 2>/dev/null; then
    echo "BOUNDARY VIOLATION: vault_ipc.h response exposes private material."
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "boundary check OK: no untrusted code links the key headers."
fi
exit $fail
