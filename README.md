# Alarm Remote

Alarm Remote is a minimal native Ubuntu Touch remote control for an Ajax alarm system.

The app is intentionally small: it focuses on everyday alarm actions and keeps background services, push notifications, room/device management, and settings work out of scope until the private Ajax API behavior is proven safely.

## Features

- QR/session login using an already logged-in official Ajax app.
- Restore of approved sessions through local protected storage.
- Space selection.
- Current alarm status display.
- Arm, disarm, and night mode commands.
- Panic command guarded by long-press and confirmation.
- Messages/event log view with category tabs.
- Rooms and devices list.
- Read-only device details for the Ajax fields currently decoded by the app.
- Foreground alarm sounds with English voice prompts for panic, intrusion, and fire events.
- Pull-to-refresh and periodic status refresh.
- Minimal AppArmor permissions: networking and audio playback.

## Important Limitations

- Ajax does not provide a public consumer API for this use case. This app uses private/undocumented Ajax API behavior discovered through research and live testing on the author's own system.
- Ajax may change login, status, or command behavior at any time.
- Device setting edit mode is present, but setting writes are not implemented yet.
- Some device fields depend on private Ajax device-specific protobuf parts. Unknown fields are shown as unavailable or disabled.
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
- `scripts/install-device.sh`: pass for `ajaxremote.cloudsite_0.2.0_arm64.click` on a connected Ubuntu Touch device.
- Foreground alarm sound playback: verified on the connected Ubuntu Touch device.
