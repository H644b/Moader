# Reporting a vulnerability

Please report vulnerabilities privately through [GitHub's private vulnerability reporting form](https://github.com/H644b/Moader/security/advisories/new). Do not open a public issue with a working exploit, live credentials or unredacted device/session data. If the form is unavailable, open a minimal issue asking the maintainer to enable a private reporting route; leave technical exploit details out until a private route is available.

Useful information includes the affected Moader version, the component involved, expected versus observed behavior, a minimal reproduction using synthetic data, and the impact you demonstrated. Separate observations from hypotheses. Redact account names, local paths, private network addresses, pairing codes, cookies, tokens, serial numbers and session identifiers. Never attach game content, firmware, keys, full memory dumps or account exports.

## Supported scope

The current release is the **v0.1.0-dev prerelease**. Reports against that release or current `main` are welcome on a best-effort basis. Older development snapshots may require updating before investigation. There is no guaranteed response time, fix deadline, long-term support policy or bounty program.

Security-sensitive areas include ZIP/path validation, browser pairing and authorization, HTTP request bounds, filesystem catalogue validation, native object lifetime, diagnostic snapshots, installation input qualification and transport uncertainty handling. A successful control request is not evidence that a game modification was applied.

The browser server is intended for a trusted local network, with pairing and explicit listener control. Do not expose it to the public internet or publish a real pairing code. Privileged runtime installation is qualified for the current boot; a console reboot requires following the documented setup again. Forced game crashes and crash recovery are not hardware-qualified by this prerelease.

Disclose the minimum information needed to reproduce the problem safely. Coordinate public details with the maintainer through the private advisory; do not assume that an acknowledgement means a fix has shipped.
