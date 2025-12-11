# Xbox Controller Plugin for PS4

A GoldHEN plugin that enables Xbox 360 USB controllers on jailbroken PS4 consoles.

## ⚠️ BETA SOFTWARE - USE AT YOUR OWN RISK ⚠️

**This plugin is experimental and may cause instability.** During development it caused jailbreak issues requiring multiple reboots. Back up your data before using.

## Status: WORKING (Single Player)

Single player mode tested and functional. The Xbox 360 controller successfully controls games on PS4!

### What Works
- Xbox 360 wired USB controller detection
- Full button mapping (A/B/X/Y, bumpers, triggers, sticks, D-pad)
- Responsive input (1ms polling, 2ms timeout)
- Replaces DS4 input in games

### NOT YET TESTED
- **Multiplayer** - Theory: Launch game with DS4, then Xbox controller might register as Player 2. Needs testing!
- Multiple Xbox controllers
- Rumble/vibration output

### Known Issues
- May cause jailbreak instability with repeated loading/unloading
- Some homebrew apps may not launch while plugin is active
- Aggressive polling settings can cause black screen crashes

## Requirements

- Jailbroken PS4 (tested on 9.00)
- GoldHEN 2.3 or newer
- Xbox 360 **wired** USB controller
- A real DS4 controller (for system menu and launching games)

## Installation

### Method 1: PKG Toggle App (Recommended)

1. Download the PKG from [Releases](https://github.com/crucifix86/xbox_controller_plugin/releases)
2. Install via GoldHEN's Package Installer (USB or Remote PKG)
3. Run "Xbox Controller Toggle" from home screen to **enable**
4. Run it again to **disable**
5. Reboot after toggling to apply changes

### Method 2: Manual FTP

1. Connect to PS4 via FTP (port 2121)
2. Upload `xbox_controller.prx` to `/data/GoldHEN/plugins/`
3. Add to `/data/GoldHEN/plugins.ini`:
   ```ini
   [default]
   /data/GoldHEN/plugins/xbox_controller.prx
   ```
4. Reboot and load GoldHEN

## Usage

1. Connect Xbox 360 controller via USB
2. Launch game with DS4
3. You should see "Xbox 360 connected!" notification
4. Xbox controller input will work in-game!

## Button Mapping

| Xbox 360 | DualShock 4 |
|----------|-------------|
| A | Cross (X) |
| B | Circle |
| X | Square |
| Y | Triangle |
| LB | L1 |
| RB | R1 |
| LT | L2 |
| RT | R2 |
| Back | Share |
| Start | Options |
| Guide | PS Button |
| L3 | L3 |
| R3 | R3 |

## Limitations

- No touchpad (Xbox controller has none)
- No motion controls (no gyro/accelerometer)
- No lightbar feedback
- No controller audio
- Requires DS4 for system menu

## Building

Requires:
- OpenOrbis PS4 Toolchain
- GoldHEN Plugins SDK

```bash
export OO_PS4_TOOLCHAIN=/path/to/openorbis

# Build plugin only
make

# Build installer PKG
make installer
```

Output:
- Plugin: `bin/xbox_controller.prx`
- Installer: `installer/IV0000-XBOX00001_00-XBOXCTRLINSTALL0.pkg`

## Technical Details

This plugin uses the gamepad_helper hooking technique:
- Hooks scePadRead and scePadReadState
- Reads Xbox controller via sceUsbd
- Translates Xbox HID reports to DS4 OrbisPadData format
- Injects translated input into the game's pad reading

## License

MIT License

## Credits

- GoldHEN team for the plugin SDK and gamepad_helper reference
- OpenOrbis team for the PS4 toolchain
