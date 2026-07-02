# Alarm Remote

Alarm Remote is a minimal native Ubuntu Touch remote control for an Ajax alarm system.

The app is intentionally small: it focuses on the everyday alarm actions and avoids background services, push notifications, room/device management, and advanced settings.

## Features

- QR/session login using an already logged-in official Ajax app.
- Restore of approved sessions through local protected storage.
- Space selection.
- Current alarm status display.
- Arm, disarm, and night mode commands.
- Panic command guarded by long-press and confirmation.
- Pull-to-refresh and periodic status refresh.
- Minimal AppArmor permissions: networking only.

## Important Limitations

- Ajax does not provide a public consumer API for this use case. This app uses private/undocumented Ajax API behavior discovered through research and live testing on the author's own system.
- Ajax may change login, status, or command behavior at any time.
- Messages/event log fetching is not implemented yet.
- Session refresh/lifetime behavior is not fully documented.
- Session material is stored in app-private protected local storage, not in a hardware-backed keychain.

## Security

Alarm Remote controls a real alarm system. Use it entirely at your own risk.

No warranty or support is provided. The app is not affiliated with or endorsed by Ajax Systems.

The app does not store passwords. QR payloads, session tokens, cookies, and private Ajax responses must never be logged or committed.

## Build

From this directory:

```sh
clickable build --arch amd64
clickable desktop --arch amd64
```

Build for Ubuntu Touch arm64:

```sh
clickable build --arch arm64
```

Install on a connected Ubuntu Touch device:

```sh
scripts/install-device.sh
```

To install over an existing build while preserving app data/session state:

```sh
~/.local/bin/clickable install --arch arm64 --skip-uninstall
```

## Verification

- `clickable build --arch amd64`: pass, click review pass.
- `clickable build --arch arm64`: pass, click review pass.
- `~/.local/bin/clickable install --arch arm64 --skip-uninstall`: pass on a connected Ubuntu Touch device.
