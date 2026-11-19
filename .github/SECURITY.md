# Security policy

LSW runs a Windows virtual machine under your user account and talks to it over a
private link. A Windows guest is treated as untrusted: nothing it sends should be
able to affect the host beyond what the user allowed.

## Reporting a vulnerability

Please report vulnerabilities privately, through
[a security advisory](https://github.com/acceleration3/linux-subsystem-windows/security/advisories/new),
not in a public issue. Include what an attacker can do, the LSW version, and the
steps or a proof of concept to reproduce it.

Once a fix is released, the advisory is published with credit to you, unless you
would rather stay anonymous.

Vulnerabilities in the GPU stack or the window mirror can be reported here too, or
directly to [accel-virt](https://github.com/acceleration3/accel-virt/security/advisories/new)
and [remotewin](https://github.com/acceleration3/remotewin/security/advisories/new).

## Supported versions

Only the latest release gets security fixes.
