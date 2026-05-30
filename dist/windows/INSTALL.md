# Installing OpenLiero on Windows

Two download options are available from each release:

- **`openliero-windows-x64.zip`** — portable, no install needed, but Windows will
  show a SmartScreen warning on first run (click "More info → Run anyway").
- **`openliero.msix`** — proper installer with Start Menu entry and clean
  uninstall via Settings → Apps. Requires trusting our self-signed publisher
  certificate first (one-time setup, steps below).

## MSIX install (recommended)

### Step 1 — Verify and trust the publisher certificate

Download `openliero-publisher.cer` from the release and verify its thumbprint
before trusting it:

```powershell
$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 "openliero-publisher.cer"
$cert.Thumbprint
# Expected: 3E2C2090E1D6A075DD8A480B1FA858E503730498
```

If the thumbprint matches, import it to the Trusted People store:

```powershell
certutil -addstore "TrustedPeople" openliero-publisher.cer
```

### Step 2 — Install the MSIX

Double-click `openliero.msix`, or from PowerShell:

```powershell
Add-AppxPackage openliero.msix
```

### Uninstalling

```powershell
Get-AppxPackage *openliero* | Remove-AppxPackage
```

Or use Settings → Apps → Installed apps → OpenLiero → Uninstall.

## Why self-signed?

OpenLiero is a free, open-source project and does not have a paid code-signing
certificate. Instead of asking you to blindly trust SmartScreen warnings, we
publish the certificate thumbprint above so you can verify it matches before
importing. The build provenance attestation (verifiable with `gh attestation
verify`) proves the artifacts were produced from the source repository by the
CI workflow.

## Where are my saves and settings?

OpenLiero stores your saves, replays, profiles, and settings in your user data
folder — **not** in the install directory. This is especially important for the
MSIX package, where the install directory is read-only.

| Install type | Save location |
|---|---|
| MSIX | `%APPDATA%\openliero\openliero\` |
| Zip | Same folder as `openliero.exe` (the zip ships with `portable.txt`) |

The zip ships with a `portable.txt` file next to the binary to keep everything
self-contained. Delete `portable.txt` to switch to the roaming profile path
above (useful if you want MSIX and zip installs to share the same saves).
