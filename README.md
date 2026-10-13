# Dual Input for Call of Juarez: Gunslinger

Gyro aiming and the right stick/trackpad at the same time, without stutter.
Without the mod the game keeps switching between mouse and gamepad, and the camera stutters.

## What it does

- In gameplay the game only sees the gamepad. Gyro is blended into the right stick.
- The right stick/trackpad has priority: while you push it, gyro is ignored.
- In menus the mouse works normally (cursor and clicks).

## Install

1. Close the game. Run `install.bat`.
2. Steam Input (controller layout for the game):
   - Gyro: **As Mouse**
   - Right trackpad / right stick: **Joystick**

**Manual install:** copy `dinput8.dll`, `xinput1_3.dll` and `xinput_mouse.ini` from `mod`
next to `CoJGunslinger.exe`.

## Uninstall

Run `uninstall.bat`, or delete `dinput8.dll`, `xinput1_3.dll`, `xinput_mouse.ini` and
`xinput_mouse.log` from the game folder.

## Settings

`xinput_mouse.ini` next to the exe. Restart the game after a change.
The in-game right-stick sensitivity applies to both gyro and trackpad.

| Key | Default | |
|---|---|---|
| `Sensitivity` | 0.3 | Gyro speed |
| `InvertY` | 1 | 1 = invert vertical gyro |
| `SmoothingMs` | 30 | Gyro smoothing (higher = steadier, lower = snappier) |
| `StickOverride` | 0.27 | How far the right stick must move to override gyro |
| `MouseButtons` | 1 | 1 = LMB fire, RMB aim, MMB RB |
| `Enabled` | 1 | 0 = mod off |

## Problems

- **No effect:** after starting the game, `xinput_mouse.log` must appear next to the exe.
- **Antivirus warning:** proxy DLLs are a common false positive. Source code is in `src`.
- Tested on the Steam version.

Source code is in `src` (build with `src\build.bat`, MSVC x86).

Unofficial fan mod, not affiliated with Techland, Ubisoft or Valve.
Use at your own risk. License: `LICENSE.txt`.
