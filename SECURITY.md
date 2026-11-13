# Security

## Reporting a vulnerability

Please report it privately through **Security → Report a vulnerability** on the
[GitHub repository](https://github.com/gilsondev/tokenpulse/security), not in a public issue. If that button is not
there, open an issue asking for a private contact, without the details. Include the board,
the firmware and daemon versions (shown in the board's About screen and web page) and the steps to reproduce.

Fixes ship as a regular release; boards and daemons update themselves from there.

## What tokenpulse reads and where it goes

- **On the computer**, the daemon (`host/tokend.py`) reads your Claude Code and Codex usage from local files and
  the Claude Code login (macOS Keychain, or `~/.claude/.credentials.json` on Linux) to ask Anthropic for the
  usage percentage. The login never leaves the computer except to Anthropic's API.
- **To the boards** it sends usage numbers, session titles, model names and organization names: over the USB
  cable, over Bluetooth LE (encrypted, bonded) or, for the SmallTV, over plain HTTP on the local network.
- It also checks GitHub for new releases and the Anthropic and OpenAI status pages; those requests carry no
  personal data. There is no telemetry.

## How the boards are protected

- **Web page:** viewing is open on the local network; changing settings, rebooting, forgetting the WiFi,
  preparing for delivery, uploading firmware and reading the screen require the board's token. A browser gets it
  by typing the 4-digit code the board shows on its own screen; the SmallTV hands it to the daemon on first contact.
- **Actions from WiFi boards:** the daemon only runs the few actions the SmallTV page offers, and only when the
  board proves the token it paired with. Switching accounts is only possible from a board on the cable.
- **Firmware updates:** the ESP32 boards roll back to the previous firmware if a new one does not boot.
- **Prepare for delivery** creates a new token, forgets the WiFi and the Bluetooth pairing.

## Known limits

- The SmallTV's first pairing trusts the first daemon that talks to it, and a device that announces itself as a
  tokenpulse board on your network can still receive the usage frames.
- The board's own setup WiFi (`tokenpulse-<codename>`) is open while it waits for your network.
- The WiFi password typed on the computer goes to USB boards over the cable only, and is never logged.

tokenpulse is an independent project, not affiliated with or endorsed by Anthropic or OpenAI.
