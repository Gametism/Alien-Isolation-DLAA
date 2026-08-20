# Alien: Isolation DLAA v1.0

Created by Gametism

## Overview

Alien: Isolation DLAA adds NVIDIA DLAA to Alien: Isolation at native rendering resolution.

The v1.0 renderer uses the configuration established during testing:

- Native-resolution DLAA
- SMAA T2x temporal pipeline integration
- 8-sample low-discrepancy temporal jitter
- Corrected/inverted motion-vector convention
- Linear depth integration
- Automatic temporal-history reset when required

DLSS Super Resolution is not included in v1.0.

## Requirements

- NVIDIA RTX graphics card with DLSS/DLAA support
- NVIDIA DLSS DLLs included with the mod
- Compatible ASI loader

### Required In-Game Graphics Settings

For DLAA to work correctly and reliably, the following in-game setting is required:

| Option | Required Value |
| --- | --- |
| Anti-Aliasing | **SMAA T2x** |

**Anti-Aliasing must be set to SMAA T2x.**

Disabling Anti-Aliasing or using an incompatible anti-aliasing mode can cause severe temporal smearing and visual breakup.

Unlike Alias Isolation, **Chromatic Aberration does not need to be disabled and Motion Blur does not need to be enabled**. These settings can be configured according to personal preference.

## In-Game Menu

Press **F7** by default to open or close the menu.

The menu is rendered directly inside the game.

Available settings:

- Enable DLAA
- Reset DLAA History
- Menu Scale: 80%, 100%, 125%, 150%
- Open/Close Menu hotkey
- Toggle DLAA hotkey
- Reset Hotkeys to Defaults
- Reset All Settings

The menu displays:

**Created by Gametism**

## Default Hotkeys

- **F7** — Open/Close Menu
- **F8** — Toggle DLAA

Both hotkeys can be rebound from the menu.

One physical key press triggers only one action; configurable hotkeys use release-latched input handling to prevent repeated toggles.

## Menu Input

While the menu is open:

- Mouse input is captured by the menu
- Keyboard input continues to reach Alien: Isolation
- Controller input continues to reach Alien: Isolation
- The menu uses its own virtual mouse cursor driven by raw mouse input during gameplay

Mouse and keyboard can both be used to operate the menu.

## Configuration

Settings are saved automatically to:

```text
AlienIsolationDLAA.ini
```

Default configuration:

```ini
[DLAA]
Enabled=1

[Hotkeys]
Menu=118
ToggleDLAA=119

[Interface]
MenuScale=100
```

## Reset DLAA History

The menu includes a manual **Reset DLAA History** command.

This is normally unnecessary, but it can be used to discard accumulated temporal history immediately without disabling DLAA.

## Reset All Settings

This restores:

- DLAA enabled
- Menu hotkey: F7
- Toggle DLAA hotkey: F8
- Menu scale: 100%

## Release Files

The compiled build produces:

```text
AlienIsolationDLAA.asi
AlienIsolationDLAA-Bridge64.exe
```

The ASI and Bridge64 executable must remain together.

An ASI loader compatible with Alien: Isolation is required.

## Notes

Rendering-critical settings are intentionally not exposed to users.

The following are fixed because they are required by the tested DLAA path:

- SMAA T2x temporal pipeline
- Inverted motion-vector direction
- 8-sample jitter sequence
- Depth source
- Motion-vector source
- Scene-color source
- DLAA injection point

Changing these would not be a normal quality preference and can cause smearing, visual breakup, instability, or incorrect temporal reconstruction.

## Credits

**Alien: Isolation DLAA** was created by Gametism.

Special thanks and credit to **Alias Isolation**, whose work on Alien: Isolation helped make this project possible.

This project incorporates and/or is derived from MIT-licensed work associated with Alias Isolation.

## MIT License

Copyright (c) Alias Isolation contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
