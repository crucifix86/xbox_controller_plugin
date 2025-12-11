# Xbox Controller Plugin for PS4

A GoldHEN plugin that enables Xbox 360 USB controllers on jailbroken PS4 consoles.

## Features

- Xbox 360 wired USB controller support
- Automatic controller detection
- Full button mapping (A/B/X/Y, bumpers, triggers, sticks, D-pad)
- Rumble/vibration support
- Up to 4 controllers simultaneously
- Configurable deadzone and button swapping

## Requirements

- Jailbroken PS4 (firmware 9.00-11.00 recommended)
- GoldHEN 2.3 or newer
- OpenOrbis PS4 Toolchain (for building)
- GoldHEN Plugins SDK

## Building

### Setup

1. Install OpenOrbis toolchain:
   ```bash
   export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis-PS4-Toolchain
   ```

2. Get GoldHEN Plugins SDK:
   ```bash
   git clone https://github.com/GoldHEN/GoldHEN_Plugins_SDK.git
   export GOLDHEN_SDK=/path/to/GoldHEN_Plugins_SDK
   ```

### Build

```bash
make
```

Output: `bin/xbox_controller.prx`

### Debug Build

```bash
make debug
```

## Installation

### Manual (FTP)

1. Connect to PS4 via FTP (port 2121)
2. Upload `xbox_controller.prx` to `/data/GoldHEN/plugins/`
3. Add to `/data/GoldHEN/plugins.ini`:
   ```ini
   [default]
   /data/GoldHEN/plugins/xbox_controller.prx
   ```
4. Reboot and load GoldHEN

### Via Makefile

```bash
PS4_IP=192.168.1.100 make install
```

## Usage

1. Connect Xbox 360 controller via USB before launching game
2. Launch game with your DS4 (required for menu navigation)
3. Xbox controller will be available as player 2/3/4

**Note**: A real DS4 is still required for:
- System menu navigation
- Launching games
- Player 1 (primary user)

Xbox controllers work as additional players for local multiplayer.

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

- No touchpad (Xbox has none)
- No motion controls (no gyro/accelerometer)
- No lightbar feedback
- No controller audio
- Requires DS4 for system menu and game launch

## Troubleshooting

### Controller not detected
- Ensure controller is connected before starting game
- Try different USB port
- Check GoldHEN notifications for errors

### Input lag or dropped inputs
- Use a quality USB cable
- Avoid USB hubs

### Game crashes
- Don't connect/disconnect controllers during gameplay
- Ensure plugin is loaded (check GoldHEN menu)

## License

MIT License

## Credits

- GoldHEN team for the plugin SDK
- OpenOrbis team for the PS4 toolchain
- Xbox controller documentation from various reverse engineering efforts
