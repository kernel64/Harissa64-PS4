# Harissa64-PS4

Harissa64 is a Nintendo 64 emulator. This is its PlayStation 4 version, for jailbroken consoles running GoldHEN.

It is a hobby project. It has been developed and tested on one console: a base PS4 on firmware 5.05. The package is the same for the PS4, the PS4 Slim and the PS4 Pro, but the other models and firmwares have not been tested by the author.

Use it only with games you own. No ROMs are included, and none will be.

## What it does

- Runs N64 games from `.z64`, `.v64`, `.n64` and `.zip` files, most of them at full speed on a base PS4.
- Internal resolution: native, x2, x4, or Auto (starts high and steps down when the console cannot keep the game's speed). It can be changed while a game runs.
- The game pauses while the app is in the background (PS button) and goes on when you come back.
- 16:9 widescreen with a wider view (not stretched), or the 4:3 picture stretched, or plain 4:3.
- The N64's own video filters (anti-aliasing, smoothing, dither filter) can each be turned off for a sharper picture; edge smoothing, sharpening, blur, scanlines and CRT effects on top.
- Save states (9 slots per game), the games' own saves (EEPROM, SRAM, FlashRAM, Controller Pak), Rumble Pak.
- Up to four controllers: each extra DualShock 4 belongs to another logged-in user of the console.
- Cheats (mupen64plus's database, plus your own files), with the GameShark button.
- RetroAchievements (softcore), with the achievements' badges.
- Box art and game information downloaded for your game list.
- Menus in English, Spanish, German, French, Arabic, Brazilian Portuguese and Russian.

## Install

1. Copy the `.pkg` of the [latest release](https://github.com/kernel64/Harissa64-PS4/releases/latest) to a USB drive, or to `/data/pkg/` on the console by FTP.
2. Install it with GoldHEN's Package Installer. When you update, delete the old version first (OPTIONS > Delete on its tile): your games, saves and settings stay, they are in `/data/Harissa64-PS4/`.
3. Start Harissa64 once: it creates its folders. Put your games in `/data/Harissa64-PS4/roms/` (FTP), or browse to them on a USB drive from the game list (OPTIONS).

Folders in `/data/Harissa64-PS4/`:

| Folder | Holds |
|---|---|
| `roms/` | your games |
| `saves/<game>/` | the game's saves and save states |
| `config/` | per-game settings, the RetroAchievements account |
| `cheats/` | your own cheat files (`<game>.txt`) |
| `art/` | downloaded box art and achievement badges |

## Controls

The N64 buttons can be remapped per controller in Settings > Controls. The front end uses:

| Button | Does |
|---|---|
| Touch pad button, short press | the in-game menu (resume, save and load state, settings, cheats, achievements, reset, game list, quit) |
| Touch pad button + R1 / L1 | save / load the state |
| Touch pad button + D-pad left / right | choose the state slot |
| Touch pad button held 3 s | back to the game list |
| R3 (right stick click) | the GameShark button |
| OPTIONS, in the game list | the file browser (USB drives, folders) |

## Known limits

- HD texture packs are not supported: the renderer (ParaLLEl-RDP) draws the N64's own textures.
- A few games are recommended at x2 because they draw high-resolution pictures; choosing x4 for them asks first.
- Heavy scenes of some games run under full speed at x4 on a base PS4: Auto, or x2, is the answer for now.
- With the low-level RSP (Settings > Advanced) widescreen is not available and most games are slower.
- Homebrew cannot award PlayStation trophies.

## Build from source

The build runs on Linux (the author uses WSL): clang 21, the PacBrew OpenOrbis toolchain, and Mesa's RADV built for the PS4 with [DolphinPS4](https://github.com/iHaiDeeZ/DolphinPS4)'s toolchain files and patch (`third_party/dolphinps4/`), plus this repository's own Mesa patch (`third_party/mesa-h64/`). With that in place:

```
bash tools/ps4/build.sh
```

makes the package. The details of the set-up follow DolphinPS4's own build instructions.

## Credits and licence

Harissa64-PS4 is licensed under the GPL v2, see `LICENSE`.

The emulation core is shared with [Harissa64 for the Xbox 360](https://github.com/kernel64/Harissa64). The graphics are drawn by ParaLLEl-RDP (Themaister) through Mesa's RADV on DolphinPS4's PS4 port of it; the audio HLE comes from mupen64plus; the RSP interpreter from ares. The full list, with every origin and licence, is in `THIRD_PARTY.md`.
