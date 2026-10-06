# Mafia Aim Assist

Lock-on aiming and full Xbox controller support for **Mafia: The City of Lost Heaven** (2002, PC).

Hold the **left trigger** (or the `O` key) and the camera glides onto the nearest person in front of you, similar to
the aim assist in Red Dead Redemption 2. The right stick also looks around like a mouse, so the game plays well on a
controller through Steam Input, on a laptop or on a handheld.

- Targets **PC version 1.0**, see [Supported versions](#supported-versions).
- Single-player only. No game files are modified or distributed; the mod adds a few DLLs next to `Game.exe`.
- Hot-reloadable logic, tiny and dependency free (no Visual C++ runtime needed).

## Drive-By Aim

Hold LT (or the configured aim key) while firing from the driver seat. Lock-on prefers police-car wheels,
then foot police, including ambient patrol officers. It excludes civilian cars, your own car and seated officers.
Search covers the full 180-degree left half-plane relative to the car, independently of camera direction.
Finding a target does not grant a lock: the conservative reachable-shot filter still requires 40 to 140 degrees
left of the car heading and elevation between -30 and +25 degrees. The former 20-degree camera acquisition
cone is removed for wheels and foot officers; camera alignment only ranks eligible targets.
Foot officers use an upper-torso aim point (1.25 metres above their origin), including targets at the driver's door.
The weapon-frame firing line must always be clear. Nearby officers do not require a clear chase-camera line,
which can be obscured by your own car; wheels and distant officers retain that additional check.
Officer LOS tolerates the body surface up to 0.35 metres before the chest aim point. A blocked/right-side target is dropped
immediately, even with `require_line_of_sight = 0`. This is geometric assistance, not a guarantee against
weapon spread, target movement or input latency. Wheel terminal-state flags are skipped.

In the game's driving controls, set accelerator to **A (Joy0 Button 1)** and brake/reverse to
**B (Joy0 Button 2)**, removing LT/RT from both primary and secondary pedal bindings.
Keep **RT (Joy0 Button 8)** for fire; the mod reads **LT** for lock-on. B crouch remains on-foot only.
The right stick remains manual aiming control in the car; on-foot target/stature flicks do not run on car targets.
Passenger seats are not supported by this driver-left implementation. Disable with `vehicle_aim = 0`.

## Experimental Sandbox

The old sandbox is disabled by default (`[sandbox] enabled = 0`): Tutorial, police and NPC behavior stay native.
The following describes the optional previous experiment, not the drive-by implementation. Its reported
explosion/NPC/crash regressions are unresolved; leave it disabled while testing ordinary missions or Free Ride.

The main-menu **Tutorial** button launches the game's native **Little Italy Free Ride** (`freeitaly`) instead
of the training sequence. Standard traffic is retained, police traffic is set to zero, and scripted police-manager
activation is suppressed for that session. The override ends when the game loop returns to the menu; ordinary
story and Free Ride sessions retain their native police behavior.

This is a host-DLL change, so restart the game after installing it. Mission files and the game executable on disk
are not modified. Aim assist and the Xbox B crouch toggle remain available.

This session also creates one hostile mafioso using the `SamHIGH` model, six metres ahead of the player's starting
position and facing away. He plays the game's cigarette-smoking animations, with the cigarette model attached
to his right-hand attachment. Automatic AI state changes are held until the player is in front and the game's
visual-contact system confirms he is seen, or the mafioso takes nonfatal damage (including from behind).
He has high aggression and morale to favor retaliation over fear. The cigarette is then hidden and the native
state switcher resumes, choosing its own valid target instead of forcing combat with an empty target slot.
The NPC is a temporary actor owned by the game and is removed with the session.

Aim target collection includes both placed and temporary actors. The two ambient gunfire-collapse calls are
skipped only in this tutorial sandbox, without forcing another crowd state; native injury/death and other modes
are unchanged. This does not cancel actual hit reactions or revive pedestrians.
Custom AI is attached to the spawned NPC instance, not the shared class vtable.

## Development Notes

See [docs/MODDING.md](docs/MODDING.md) for verified engine facts, NPC personality parameters, ABI lessons and
the next steps for safe event-driven spawning. [config/mafia-1.0.profile.json](config/mafia-1.0.profile.json) records
binary signatures and calling conventions. Validate the supported disk EXE with
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/check-game.ps1`.

## Install

1. Download the latest `MafiaAimAssist-x.y.z.zip` from the Releases page and extract it.
2. Find the folder that contains `Game.exe` and `LS3DF.dll`
   (Steam: `...\steamapps\common\Mafia\Mafia`, GOG: the folder you installed to).
3. Copy these files from the zip's `files` folder into that folder:
   - `dinput8.dll`, `MafiaAimLogic.dll`, `MafiaAimAssist.ini`
   - Controller with Steam Input on Windows: also everything from `files\xidi` (`dinput.dll`, `Xidi.32.dll`,
     `Xidi.ini`)
4. Set up the in-game controls once, as described below.

If the game is under `Program Files` and Windows refuses to copy, run the copy as administrator.

`dinput8.dll` is a proxy: the game loads it instead of the system DirectInput 8 library, and it forwards everything to
the real one. If the folder already has a `dinput8.dll` from another mod (for example an ASI loader or the Widescreen
Fix), the two cannot be used together; keep a backup of the original before overwriting it.

To remove the mod, delete the files you copied (and restore any `dinput8.dll` you backed up).

## Required: in-game controls

The mod feeds its camera movement through the game's mouse input, so the **Aim** control must listen to the mouse.
In the game go to **Options > Controls > character controls** (Polish: *Sterowanie postacią*) and set:

| Action (Polish) | Primary (*Podstawowe*) | Secondary (*Dodatkowe*) |
| --- | --- | --- |
| Aim (*Celowanie*) | `Mouse - Y axis` | `Mouse - X axis` |

To assign an axis, select the field and move the mouse along the wanted axis (up and down for Y, left and right for X).

### Controller bindings (Xbox controller)

Bind movement to the **left stick**; the game reads it as Joy0. The right stick and the left trigger are handled by the
mod, so leave them unassigned.

| Action | Binding |
| --- | --- |
| Forward / Backward | `Joy0 / Y Axis / -` and `Joy0 / Y Axis / +` |
| Left / Right | `Joy0 / X Axis / -` and `Joy0 / X Axis / +` |
| Look around | right stick (handled by the mod) |
| Lock-on aim | hold the left trigger (handled by the mod) |
| Aim height while locked | Flick right stick up for head height or down for lower torso; repeat the same direction to return to torso |

Buttons are numbered by Xidi like this: A = 1, B = 2, X = 3, Y = 4, LB = 5, RB = 6, LT = 7, RT = 8, Back = 9,
Start = 10, left stick click = 11, right stick click = 12. A comfortable layout is Fire = `Joy0 / Button 8` (RT),
Action = Button 3 (X), Jump = Button 1 (A), Crouch = Button 2 (B), Reload = Button 4 (Y).

With `crouch_toggle = 1`, B is read directly through XInput and does not require an in-game crouch binding.
Press B once to crouch, release it to stay crouched, and press it again to stand. The game retains its own
clearance and carried-object checks. With `crouch_toggle = 0`, the game's native crouch binding is used.

## Steam and Xbox controller on Windows

1. Connect the controller to Windows (USB or Bluetooth) **before** starting the game.
2. In Steam open the game's **Properties > Controller** and set Steam Input to **Enable Steam Input**.
3. Open the game's controller configuration and pick the plain **Gamepad** layout (not "Keyboard and mouse").
4. Make sure the Xidi files from `files\xidi` were copied next to `Game.exe`.
5. Launch the game **from Steam**, set the controls from the sections above, and play.

The game only knows old DirectInput controllers. Steam Input hides the pad from DirectInput and shows an XInput
controller instead; Xidi translates it back for the left stick and buttons, and the mod reads XInput directly for
the right stick and the trigger.

## Steam Deck, Legion Go and other SteamOS devices

Proton's own DirectInput already sees the controller, so Xidi is **not** needed.

1. Copy `dinput8.dll`, `MafiaAimLogic.dll` and `MafiaAimAssist.ini` from the zip's `files` folder into the game folder
   (in desktop mode: `~/.local/share/Steam/steamapps/common/Mafia/Mafia/`).
2. In Steam open the game's **Properties > General > Launch options** and enter:

   ```
   WINEDLLOVERRIDES="dinput8=n,b" %command%
   ```
3. Set the in-game controls as described above and start the game.

This path is expected to work but has had less testing than Windows. Please report what you see, including the
logs listed under [Troubleshooting](#troubleshooting).

## Settings

`MafiaAimAssist.ini` (next to `Game.exe`) is re-read every second, so you can change it while playing.

| Key | Default | Meaning |
| --- | --- | --- |
| `aim_height_cm` | `95` | Base aim point above the target's feet. While locked, flick the right stick up to raise the aim point toward the head, down for the lower torso, or repeat a flick to return to this base height. |
| `animated_head_aim` | `1` | Use the target's animated head/neck capsule when head aim is selected; fall back to a height offset if the model has no usable skeleton. Set `0` to disable. |
| `animated_head_forward_cm` | `8` | Horizontal head-point offset toward the target's facing direction, in centimetres (`0` to `25`). Set `0` to disable. |
| `experimental_crouch_head_aim` | `1` | Fallback only: lower the height-based head point when the target's `+0x1e4` byte is nonzero. |
| `aim_key` | `O` | Keyboard key for lock-on. Set one letter/digit, `F1`-`F12`, `SPACE`, `ENTER`, `TAB`, `ESC`, `SHIFT`, `CTRL`, `ALT`, `CAPSLOCK` or `BACKSPACE`. The left trigger remains enabled. |
| `crouch_toggle` | `1` | Toggle crouch with Xbox B while on foot, through the game's normal player update. The 1.0 callsite and setter signatures are validated before hooking. Set `0` to disable. |
| `vehicle_aim` | `1` | Driver-left police wheel/foot-officer lock-on; mandatory firing-line checks. Passenger seats are excluded. |
| `aim_response_percent` | `70` | Lock-on strength. Lower it for a slower approach; accepted range is `25` to `150`. Changes are read while the game runs. |
| `require_line_of_sight` | `1` | Require an unobstructed collision line to acquire or keep a target. Set `0` to disable. If the supported game function cannot be validated, the mod logs the issue and falls back to the previous targeting behavior. |
| `prioritize_enemies` | `1` | Prefer scripted mission enemies (AI group 4) when acquiring or manually switching. A locked target is not automatically replaced. Falls back to another eligible person if no group-4 target is available; set `0` to disable. |
| `target_cone_degrees` | `20` | Acquisition cone for ordinary NPCs, in degrees (`5` to `45`). |
| `enemy_target_cone_degrees` | `35` | Acquisition cone for mission enemies, in degrees (`5` to `60`). |
| `target_switch_stick` | `2` | While locked on, make a horizontal-dominant flick to switch to the nearest person on that side. Vertical-dominant flicks are reserved for aim zones; switches have a short debounce. After losing a target, release and press the aim button again to acquire another. `2` right stick, `1` left stick, `0` off. |
| `right_stick_look` | `1` | Right stick moves the camera. Set `0` to disable. |
| `look_x_speed`, `look_y_speed` | `1100`, `1000` | Camera speed at full deflection, mouse counts per second. |
| `invert_y` | `0` | `1` inverts the vertical look direction. |
| `stick_deadzone` | `15` | Stick drift filter in percent of the travel. |

## Troubleshooting

- **Nothing happens when holding the trigger:** check that the Aim control is `Mouse - Y axis` / `Mouse - X axis`
  and that a person is within about 80 metres in front of you. Drive-by lock requires a firearm, the driver seat
  and a reachable police target on the left.
- **The controller does nothing at all:** connect it before launching, enable Steam Input with the Gamepad layout,
  and confirm the Xidi files are installed. Without Steam Input, Windows exposes the controller to the game directly.
- **Camera spins or aim drifts the wrong way:** delete `%TEMP%\MafiaAimGain.cal` and try again; the mod relearns
  your mouse sensitivity in a few seconds of play.
- **Logs:** `%TEMP%\MafiaAimLogic.log` and `%TEMP%\MafiaAimHost.log`. On SteamOS they are in the Proton prefix
  (`.../compatdata/<appid>/pfx/drive_c/users/steamuser/Temp/`).

## Supported versions

The mod reads the game's memory at fixed addresses, so it only supports one executable: `Game.exe` with
SHA-256 `9f3c6ee5c0c5629a076cd22fb777c0ea0b889f1be3e093780a357a7025ed3dda`, the user's PC 1.0 build.
This branch no longer targets the previous Steam/GOG 1.3 executable. No game executable is distributed.

### Verified 1.0 profile

Addresses below are RVAs relative to `Game.exe`. They were checked against the installed executable and the
PC export in [mafia-reverse-engineering-export](https://github.com/st0rm94/mafia-reverse-engineering-export).

| Surface | RVA / layout |
| --- | --- |
| Active mission pointer | `0x25115C`; mission `+0x24` holds `C_game` |
| Player and camera | `C_game+0xE4` player; `C_game+0x50` camera frame; `+0x40` running flag |
| Actor list | Mission `+0x38` begin, `+0x3C` end; pointer elements |
| NPCs | Actor kind `+0x10`; health `+0x644`; mission AI group `+0xF4C` |
| LOS | `0x190A30`, `thiscall`, collision object `0x27A588`, six stack arguments, `ret 0x18` |
| Crouch | `0x09FDF0`, `thiscall`, one boolean stack argument, `ret 4` |
| Player crouch call | `0x0C9183`, original bytes `E8 68 6C FD FF`; only this on-foot call is redirected |
| Tutorial menu result | `0x176AA7`, `B8 11 00 00 00`; action `0x11` resolves to the `tutorial` scene and is redirected to native FreeItaly action `0x1C` |
| Free Ride configuration | `0x1608F0`; five stack arguments, `ret 0x14`; default model/flags/traffic with police ratio zero |
| Script police activation | `0x1C01F9`, call to `0x1CC9E0`; three stack arguments; activation forced off only during tutorial-launched Free Ride |
| Session end | `0x1F9FD4`, call to `0x1F9540`; clears the tutorial-specific override when the normal game loop returns |

The stable host owns the crouch hook. Reloadable logic only chooses the boolean passed to the original game
method; it does not write the stance byte or invoke that method from the mouse callback.
The native x86 regression harness uses simulated world memory and an ABI-compatible setter, not the game's
animation/collision engine or a full mission launch. Run it with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools/cleanbuild.ps1 -Test`.

## Known limitations

- Enemy priority uses the mission AI group, not a universal friend/foe flag. If no eligible group-4 target is in
  the search area, the mod falls back to another living pedestrian, which may be a civilian.
- Line-of-sight filtering uses the supported Game.exe 1.0 collision query. If its wrapper signature does not match,
  filtering is disabled and the previous targeting behavior is retained.
- Drive-by support is driver-seat only and conservatively filters shot geometry; full in-game verification is pending.
- It replaces `dinput8.dll`, so it cannot be combined with other mods that use the same file name.

## How it works

`dinput8.dll` is loaded by the game's engine and hooks the DirectInput mouse device. Each time the game reads the
mouse, the mod adds the right stick's movement and, while the lock-on is held, a correction toward the target. The
correction is a closed loop: it reads the camera direction and the target's position from the game, and learns how many
radians of camera turn one mouse count produces so it works at any sensitivity. Because it only adds mouse input, the
game's own camera and aiming code does all the real work. `MafiaAimLogic.dll` is loaded by the proxy as a copy, which
lets the logic be replaced while the game runs.

## Build from source

Requires Visual Studio with the C++ desktop tools. Run `build.bat`; it builds the two DLLs, downloads Xidi
(verifying its SHA-256) and writes `dist\MafiaAimAssist-<version>.zip`.

```
src\MafiaAimLogic.c     aim logic, right stick, sensitivity learning (MafiaAimLogic.dll)
src\MafiaAimHost.c      DirectInput proxy and hot reload host (dinput8.dll)
config\                 default MafiaAimAssist.ini and Xidi.ini
```

## License

MIT, see `LICENSE`. Bundled Xidi is BSD 3-Clause, see `THIRD_PARTY_NOTICES.md`. This project is not affiliated with
2K Games, Hangar 13 or Illusion Softworks.
