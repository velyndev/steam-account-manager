# Security Policy

## Reporting a vulnerability

Please report security issues privately. **Do not open a public issue for a
vulnerability.**

Email **cloud@luminary.pub** with:

- a description of the issue and its impact,
- steps to reproduce (or a proof of concept),
- the app version (Settings > About shows it, or the release tag) and your Windows version.

You can expect an acknowledgement within a few days. Please give a reasonable
window to ship a fix before any public disclosure.

## Supported versions

Only the latest release on the [Releases](../../releases) page receives fixes.

## How your data is protected

- **Encrypted vault.** Every account secret (Steam password, refresh tokens,
  Steam Guard `shared_secret` / `identity_secret`, session data, per-account proxies)
  lives in `vault.bin`, encrypted with **AES-256-GCM**. The key is derived from your
  master password with **PBKDF2-HMAC-SHA256, 600,000 iterations**.
- **No recovery path.** The master password is never stored (unless you opt into
  the DPAPI cache below). If you lose it, the vault cannot be decrypted. There is
  no backdoor.
- **Local only.** The vaults, settings, and logs stay on your machine, under
  `%LOCALAPPDATA%\steam-account-manager` (in a `data` folder next to the executable in
  portable mode, or in the data folder you moved them to). Nothing is uploaded.
- **No telemetry.** The app talks to Steam for what you ask for or turn on (refreshing
  account data, signing in, confirmations, trades, CS2 data, the Steam time sync for
  codes, and the background checks you enable). Besides Steam it contacts GitHub for the
  release check at launch (off in Settings > About) and for the CS2 item names on
  `raw.githubusercontent.com` (SteamDatabase GameTracking-CS2 and ByMykel CSGO-API), and
  `api.ipify.org` when you press Test the proxy.

## What is not encrypted

Only `vault.bin` is encrypted. These files sit next to it as plain text, readable by
anyone with access to your Windows account or the data folder:

- **`settings.json`**, one for the app and one per vault. The vault's file holds the
  **Steam Web API key** and the **single proxy address, including its user name and
  password** when you enter them. The app's file holds a copy of that proxy address
  too, because the update check runs before a vault is open. Both also hold saved
  trade links and the rest of your settings.
- **`conf_audit.json`** and **`trade_audit.json`** in each vault's folder: the history
  of confirmation decisions and of sent trade offers, with account logins.
- **`notifications.json`** in each vault's folder: detected ban and cooldown changes.
- **`vaults\registry.json`**: vault names, colors and icon file names.

Use per-account proxies (stored in the vault) instead of the single proxy if the proxy
credentials must not sit in plain text.

## Trade-offs to understand

- **DPAPI auto-unlock.** Enabling "Open the vault without the password" caches the
  master password with Windows DPAPI so the vault opens without a prompt. This trades
  protection for convenience: anyone signed in as the same Windows user on that machine
  can then open the vault without knowing the master password. Leave it off if that is
  not acceptable. The background refresh at Windows logon turns it on, because it has
  to open the vault unattended.
- **Administrator elevation.** On by default, and can be turned off in
  Settings > General. Three things need it: the Windows-logon task, writing into a
  Steam folder your account has no write access to, and controlling a Steam that is
  itself running elevated. Everything else (sign-in, Steam Guard, confirmations,
  trades, the CS2 tools) works without it. Steam's own settings live under
  `HKEY_CURRENT_USER`, so no part of the login flow needs elevation.
- **Auto-approve.** With auto-approve on (Settings > Steam Guard), market listings
  and phone number changes, whichever you enable, are approved as soon as they are
  fetched, with no chance to check them. Trades are never approved automatically.
- **No code signing.** Release binaries are unsigned, so Windows SmartScreen warns
  on first run. Verify the SHA-256 published with each release if you want to
  confirm the download.
- **The tracer cleaner deletes permanently.** Nothing it removes is backed up and
  there is no undo, so the keep list in Settings > Cleaner is the only thing standing
  between an account and a wipe: anything not ticked there is signed out of this PC,
  and under Full Wipe loses its `userdata` folder (cloud saves, game settings, CS2
  configs, controller bindings, screenshots). It is off by default, every trigger is
  opt-in, and a run is only offered after Preview has listed exactly what it deletes.
  It only touches Steam's folders, Steam-related crash dumps under
  `%LOCALAPPDATA%\CrashDumps`, and `HKEY_CURRENT_USER`, never the vault.

This tool ships no cheats, scripts, or game modifications. The optional "launch CS2
with an external loader" method only runs a loader executable that you supply and
point it at; nothing of the sort is bundled or downloaded.
