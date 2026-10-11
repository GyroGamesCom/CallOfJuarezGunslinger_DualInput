# Dual Input mod for Call of Juarez: Gunslinger

Use **gyro aiming and a stick/trackpad at the same time**, without stutter.

Out of the box, Gunslinger switches between "mouse" and "gamepad" whenever either
one moves. Steam Input sends gyro as mouse, so on Steam Deck, Steam Controller,
DualSense, Switch Pro and similar controllers, the two inputs keep fighting and
the camera stutters. This mod fixes that:

- **In gameplay**, the game only sees the gamepad. Gyro (mouse) movement is blended
  into the right stick, so gyro and the right trackpad/stick work together.
- **The right stick/trackpad wins.** Once you push it, gyro is ignored until you
  let go, so big turns stay clean and gyro handles fine aim.
- **In menus** (main and pause menu), the mouse goes to the game normally, so the
  cursor and clicks work.
- Everything else (left stick, buttons, vibration) passes through untouched.

Unofficial fan mod. Not affiliated with Techland or Ubisoft. Use at your own risk.

## Requirements

- Call of Juarez: Gunslinger, **Steam version** (tested on the current Steam build).
  Other versions may work, but the automatic menu-cursor switch turns itself off
  if the game differs.
- A controller with gyro, used through **Steam Input**.

## Install

Download the latest ZIP from the [Releases](https://github.com/GyroGamesCom/CoJ_DualInput/releases) page.

**Automatic:** unzip anywhere and run `install.bat`. It finds the game through
Steam (or asks for the folder), backs up any files it would replace, and keeps
your existing settings if you update.

**Manual:** copy the 3 files from the `mod` folder (`dinput8.dll`, `xinput1_3.dll`,
`xinput_mouse.ini`) next to `CoJGunslinger.exe`
(Steam: right-click the game, then Manage > Browse local files).

**Uninstall:** run `uninstall.bat`, or delete `dinput8.dll`, `xinput1_3.dll`,
`xinput_mouse.ini` and `xinput_mouse.log` from the game folder.

## Steam Input setup

In the game's controller configuration:

| Input              | Set to                                 |
|--------------------|----------------------------------------|
| Gyro               | **As Mouse** (any activation you like) |
| Right trackpad / stick | **Joystick** (right stick output)  |
| Everything else    | Normal gamepad layout                  |

Tune each input separately:

- **Gyro speed:** Steam Input gyro sensitivity, or `Sensitivity` in `xinput_mouse.ini`.
- **Trackpad speed:** Steam Input trackpad settings.
- **The game's own right-stick sensitivity / invert** applies to *both* gyro and
  trackpad (gyro is fed through the right stick). Set it so gyro feels right,
  then tune the trackpad in Steam.
- The in-game mouse sensitivity only affects the menu cursor.

If vertical gyro is inverted, change `InvertY` in the ini (or invert in Steam,
but not both, or they cancel out).

## Settings (`xinput_mouse.ini`)

Changes take effect after restarting the game. Every option is explained in the file.

| Key            | Default | Meaning |
|----------------|---------|---------|
| `Enabled`      | 1   | 1 = mod on, 0 = off |
| `Sensitivity`  | 0.3 | Gyro speed (1.0 = about 1000 mouse counts/s gives a fully pushed stick) |
| `SensitivityY` | 1.0 | Extra multiplier for vertical gyro |
| `InvertY`      | 1   | 1 = invert vertical gyro |
| `Curve`        | 1.0 | 1.0 linear, below 1 boosts small moves, above 1 makes them finer |
| `DeadzoneComp` | 0.10 | Pushes small gyro moves past the game's stick dead zone |
| `SmoothingMs`  | 30  | Gyro smoothing window (5-200 ms). Higher = steadier, lower = snappier |
| `StickOverride`| 0.27 | Right-stick deflection above which gyro is ignored |
| `MouseMode`    | 0   | 0 = gyro through the right stick (recommended), 1 = game's native mouse look |
| `MouseButtons` | 1   | 1 = LMB fire (RT), RMB aim (LT), MMB RB |
| `PassWheel`    | 1   | 1 = mouse wheel still reaches the game |
| `MenuMouse`    | 1   | 1 = normal mouse cursor in menus |
| `ToggleKey`, `ModeKey`, `SlowerKey`, `FasterKey` | 0 | Optional hotkeys (virtual-key codes, e.g. 80 = P): mod on/off, switch MouseMode, gyro slower/faster by 10% (saved) |

## Troubleshooting

- **Nothing changes:** check that `xinput_mouse.log` appears in the game folder after
  launching. If not, the DLLs are in the wrong folder.
- **Antivirus warning:** proxy DLLs next to a game exe are a common false positive
  (ReShade and many fixes work the same way). The full source code is in `src`.
- **Conflict with another mod** that also uses `dinput8.dll` or `xinput1_3.dll`:
  the installer backs it up to `DualInput_backup` and restores it on uninstall.
  The two cannot run at the same time.

## How it works

Two small proxy DLLs sit between the game and Windows:

- `dinput8.dll` wraps the game's DirectInput mouse. During gameplay it takes mouse
  motion and buttons away from the game and hands them to the second DLL.
- `xinput1_3.dll` forwards the real controller state (including Steam Input's
  virtual pad) and mixes the mouse into the right stick. Steam's overlay detours
  the exported `XInputGetState`, so the mod points the game's import entry at its
  own function, which calls Steam's version first and then mixes.
- Menu detection reads the game's menu state, so the cursor works in menus.

Build: run `src\build.bat` (needs Visual Studio Build Tools, 32-bit MSVC).

## Contact

Bug reports, questions and permission requests: open an issue at
https://github.com/GyroGamesCom/CoJ_DualInput/issues

## License

Free for personal use. Redistribution, re-uploading or any commercial use requires prior written
permission from GyroGamesCom, and GyroGamesCom must always be credited. Showing the mod in
videos and streams is fine with credit. See `LICENSE.txt` for the full terms.
