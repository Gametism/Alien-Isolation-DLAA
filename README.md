# Alien: Isolation DLAA v1.0

Created by Gametism

## Overview

Alien: Isolation DLAA adds NVIDIA DLAA to Alien: Isolation at native rendering
resolution.

The mod provides selectable NVIDIA DLAA render presets and configurable
sharpness directly through its in-game menu.

Default configuration:

- DLAA Preset: **K**
- Sharpness: **0.35**

## Requirements

- NVIDIA RTX graphics card with DLSS/DLAA support
- NVIDIA DLSS DLLs included with the mod
- Compatible ASI loader

### Required In-Game Graphics Setting

Anti-Aliasing must be set to:

**SMAA T2x**

Disabling Anti-Aliasing or using an incompatible anti-aliasing mode can cause
severe temporal smearing and visual breakup.

Chromatic Aberration and Motion Blur can be configured according to personal
preference.

## In-Game Menu

Default controls:

- **F7** — Open/Close Menu
- **F8** — Toggle DLAA

Available settings:

- Enable DLAA
- DLAA Preset
- Sharpness
- Reset DLAA History
- Menu Scale
- Open/Close Menu hotkey
- Toggle DLAA hotkey
- Reset Hotkeys to Defaults
- Reset All Settings

## DLAA Presets

Four NVIDIA DLAA render presets can be selected:

- **J** — Alternative transformer preset that can reduce ghosting in some scenes, but may introduce more flickering.
- **K** — Default. NVIDIA's recommended/default preset for DLAA.
- **L** — Alternative transformer preset normally associated with Ultra Performance.
- **M** — Alternative transformer preset normally associated with Performance.

Changing the preset recreates the DLAA feature and resets temporal history.

Preset K remains the default because it is NVIDIA's designated DLAA preset and
provides the safest general-purpose image-quality configuration. Presets L and
M are available for users who prefer their visual characteristics.

## Sharpness

Sharpness can be adjusted from:

```text
Off
0.05
0.10
...
0.50
```

`Off` is equivalent to a sharpness value of `0.00`.

Default:

```text
0.35
```

Changing sharpness recreates the DLAA feature and resets temporal history.

## Configuration

Settings are stored in:

```text
AlienIsolationDLAA.ini
```

Default configuration:

```ini
[DLAA]
Enabled=1
Sharpness=0.35
Preset=K

[Hotkeys]
Menu=118
ToggleDLAA=119

[Interface]
MenuScale=100
```

## Reset All Settings

This restores:

- DLAA enabled
- DLAA Preset: K
- Sharpness: 0.35
- Menu hotkey: F7
- Toggle DLAA hotkey: F8
- Menu scale: 100%

## Menu Input

While the menu is open:

- Mouse input is captured by the menu
- Keyboard input continues to reach Alien: Isolation
- Controller input continues to reach Alien: Isolation
- The overlay uses its own virtual raw-input mouse cursor during gameplay

## Notes

The following rendering-critical settings remain fixed:

- SMAA T2x temporal pipeline
- Inverted motion-vector direction
- 8-sample jitter sequence
- Depth source
- Motion-vector source
- Scene-color source
- DLAA injection point

## Credits

Alien: Isolation DLAA was created by **Gametism**.

Special thanks and credit to **Alias Isolation**, whose work on Alien:
Isolation helped make this project possible.

This project incorporates and/or is derived from MIT-licensed work associated
with Alias Isolation.

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
