# Xbox Controller Plugin for PS4

A GoldHEN plugin that enables Xbox 360 USB controllers on jailbroken PS4 consoles.

## Status: WORKING (Single Player + Local Multiplayer)

Xbox 360 controller works as Player 2 in local multiplayer games! DS4 remains Player 1.

### What Works
- Xbox 360 wired USB controller detection
- Full button mapping (A/B/X/Y, bumpers, triggers, sticks, D-pad)
- Responsive input (1ms polling, 2ms timeout)
- **Local multiplayer** - Xbox controller as Player 2, DS4 as Player 1

### Not Supported
- Rumble/vibration output
- Wireless Xbox controllers

## Requirements

- Jailbroken PS4 (tested on 9.00)
- GoldHEN 2.3 or newer
- Xbox 360 **wired** USB controller
- A real DS4 controller (for system menu and Player 1)

## Installation

1. Connect to PS4 via FTP (port 2121)
2. Upload `xbox_controller.prx` to `/data/GoldHEN/plugins/`
3. Create or edit `/data/GoldHEN/plugins.ini`:
   ```ini
   [default]
   /data/GoldHEN/plugins/xbox_controller.prx
   ```
4. Launch a game - plugin loads automatically

To disable: Remove the plugin line from `plugins.ini`

## Multiplayer Setup

For local multiplayer to work:

1. **Create a second PS4 user profile** (Settings > Users > Create User)
2. **Log in both users** before launching the game:
   - Your main profile (Player 1 - DS4)
   - Second profile (Player 2 - Xbox controller)
3. Launch the game
4. You should see "Xbox 360 connected!" and "Xbox Player 2 ready!" notifications
5. Both controllers now work independently!

**Important**: Both users must be logged in at the PS4 system level. The plugin injects the Xbox controller as the second user's controller.

**Note**: The second user ID is currently hardcoded. If multiplayer doesn't work, check `/user/home/` via FTP and update `XBOX_VIRTUAL_USER_ID` in `src/hooks.c` with your second profile's hex ID.

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
- Second user ID is hardcoded (may need manual configuration)

## Building

Requires:
- OpenOrbis PS4 Toolchain
- GoldHEN Plugins SDK

```bash
export OO_PS4_TOOLCHAIN=/path/to/openorbis
make
```

Output: `bin/xbox_controller.prx`

## Technical Details

This plugin hooks multiple PS4 system functions:
- `scePadOpen` / `scePadClose` - Virtual controller registration
- `scePadRead` / `scePadReadState` - Input injection
- `scePadGetControllerInformation` - Controller status
- `sceUserServiceGetLoginUserIdList` - User injection for multiplayer
- Reads Xbox controller via `sceUsbd` USB API
- Translates Xbox HID reports to DS4 OrbisPadData format

## License

MIT License

## Credits

- GoldHEN team for the plugin SDK and gamepad_helper reference
- OpenOrbis team for the PS4 toolchain
- remotePad plugin for virtual controller architecture inspiration
