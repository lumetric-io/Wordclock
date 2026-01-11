# Word Clock Firmware

[![Version](https://img.shields.io/badge/version-26.1.5-green.svg)](https://github.com/lumetric-io/Wordclock/releases)
[![License](https://img.shields.io/badge/license-Proprietary-red.svg)](LICENSE)

ESP32-based word clock firmware with extensive IoT capabilities, featuring Home Assistant integration, OTA updates, and a comprehensive web interface.

![Word Clock](docs/images/wordclock_demo.jpg)

## TL;DR

1. Download the latest release binary from the [GitHub releases page](https://github.com/lumetric-io/Wordclock/releases).
2. Flash the ESP32:  
   ```bash
   esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
     write_flash -z 0x1000 wordclock-v26.1.5.bin
   ```
3. Power-cycle the clock and connect to the temporary Wi-Fi network `Wordclock_AP` to finish setup.

## Project Status

- ✅ **Actively developed** - Regular updates and improvements
- ✅ **Stable releases** - Production-ready firmware available
- ✅ **Auto-updates** - Nightly firmware checks (configurable)
- ⚠️ **Breaking changes** - Possible between major versions

## Features

### Display
- 🌐 **Multi-language support:** Dutch, English (extensible)
- 📐 **Multiple grid variants:** 11x11, 20x20, 50x50 layouts
- 🎨 **RGBW LEDs:** Full color customization
- ✨ **Smooth animations:** Classic and smart animation modes
- 🌙 **Night mode:** Scheduled dimming or display off

### Connectivity
- 📡 **WiFi:** WPA2 with configuration portal
- 🏠 **Home Assistant:** Full MQTT discovery integration
- 🔄 **OTA Updates:** Wireless firmware and UI updates
- 🌐 **Web UI:** Complete configuration interface
- 📝 **Logging:** File-based logs with retention

### Advanced
- ⏰ **NTP Time Sync:** Automatic timezone support
- 🔐 **Authentication:** Web UI and MQTT security
- 🎯 **Sell Mode:** Demo display (10:47)
- 📊 **Diagnostics:** System health monitoring
- 🔧 **Customizable:** Extensive configuration options

## Quick Start

### Hardware Requirements
- ESP32 development board (4 MB flash recommended)
- WS2812B or SK6812 RGBW LED strip
- 5V power supply (3-5A depending on LED count)
- Optional: Light sensor for auto-brightness

### Software Requirements
- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- USB cable for initial programming

### Installation

1. **Clone the repository:**
   ```bash
   git clone https://github.com/lumetric-io/Wordclock.git
   cd Wordclock
   ```

2. **Configure:**
   ```bash
   # Copy example configuration
   cp include/secrets_template.h include/secrets.h
   # Edit secrets.h with your settings
   ```

3. **Build and upload:**
   ```bash
   # Using PlatformIO
   pio run -t upload
   
   # Upload filesystem
   pio run -t uploadfs
   ```

4. **Connect:**
   - Connect to `Wordclock_AP` WiFi network
   - Navigate to http://192.168.4.1
   - Configure WiFi credentials
   - Clock will restart and connect

5. **Access:**
   - Web UI: http://wordclock.local
   - MQTT: Configure in web UI settings

For detailed setup instructions, see [DEPLOYMENT.md](docs/DEPLOYMENT.md).

## Hardware & Wiring

Refer to [docs/setup/BuildInstruction.md](docs/setup/BuildInstruction.md) for wiring notes. At minimum you need:
- ESP32 DevKit or equivalent (4 MB flash recommended)
- 5 V power supply tied to `VIN` and `GND`
- NeoPixel/WS2812 LED matrix connected to GPIO `D4` (default)

**LED Mounting Pattern:**
- Start at upper-right corner
- Skip the first LED (index 0)
- Leave four LED gaps when turning corners

See [LED_MAPPING.md](docs/LED_MAPPING.md) for detailed grid layouts.

## Firmware Options

- **Pre-built binary**: Each tagged release publishes `wordclock-vX.Y.bin` plus a `firmware.json` manifest that OTA clients consume.
- **Local build**: Use PlatformIO (`platformio run -t upload`) after cloning the repository and configuring secrets (see below).
- **OTA bundles**: Static dashboard assets live under `data/`; they are uploaded with `platformio run -t uploadfs` or via the web-based upgrader.

## Configuration

1. Copy `include/secrets_template.h` to `include/secrets.h` and set Wi-Fi credentials and optional MQTT details.
2. Duplicate `upload_params_template.ini` as `upload_params.ini` for per-device OTA and serial upload parameters.
3. Adjust grid layout defaults in `src/wordclock_system_init.cpp` if you ship with a non-default language layout.

## Admin & Daily Use

- Access the dashboard at `http://wordclock.local` or by IP to adjust brightness, animations, and scheduling.
- The admin UI exposes Wi-Fi reset, password management, manual firmware upload, and live logs.
- Time synchronizes automatically via NTP; no RTC battery is required.
- Nightly OTA checks occur at 02:00 local device time. Override the manifest URL in `src/config.h` if you host your own updates.

## Development Notes

| Path                     | Purpose                                                                    |
|--------------------------|----------------------------------------------------------------------------|
| `src/`                   | Core firmware modules (`main.cpp`, subsystems, grid layouts)               |
| `data/`                  | Web dashboard and admin static assets (served from SPIFFS)                 |
| `include/`               | Public headers, secrets template, feature flags                            |
| `lib/`                   | External and custom reusable libraries                                     |
| `tools/`                 | Utility scripts such as OTA deployment helpers                             |
| `docs/`                  | Comprehensive documentation (release, development, technical, setup)       |

Key modules to inspect first:
- `src/main.cpp` bootstraps networking, OTA, web, and display subsystems.
- `src/grid_layout.cpp` contains the available word-clock layouts and helper lookups.
- `src/wordclock_system_init.cpp` wires settings, authentication, and web routes together.

## Documentation

Comprehensive documentation is organized by topic:

### User Guides
- 📚 [Architecture Overview](docs/ARCHITECTURE.md) - System design and components
- 🚀 [Deployment Guide](docs/DEPLOYMENT.md) - Installation and configuration
- 🏠 [Home Assistant Integration](docs/MQTT_INTEGRATION.md) - MQTT setup
- 💡 [LED Mapping](docs/LED_MAPPING.md) - Grid variant system
- 🔍 [Troubleshooting](docs/TROUBLESHOOTING.md) - Common issues

### Developer Resources
- 🛠️ [Development Guide](docs/DEVELOPMENT.md) - Building and contributing
- 📖 [API Reference](docs/api/html/index.html) - Code documentation (generate with Doxygen)
- 🤝 [Contributing Guidelines](docs/CONTRIBUTING.md) - How to contribute
- 📝 [Changelog](docs/CHANGELOG.md) - Release history

### Additional Documentation
- **[QuickStart Guide](docs/setup/QuickStart.md)** - End-user setup walkthrough
- **[Build Instructions](docs/setup/BuildInstruction.md)** - Physical assembly and wiring
- **[Release Pipeline](docs/release/START_HERE_RELEASE.md)** ⭐ - Creating and managing releases
- **[Full Documentation Index](docs/DOCUMENTATION_INDEX.md)** - Complete documentation overview

## Home Assistant Integration

The word clock provides full MQTT discovery for Home Assistant:

**Entities:**
- Light control (brightness, color)
- Animation toggle
- Night mode settings
- System sensors (version, IP, RSSI)
- Diagnostic buttons

See [MQTT_INTEGRATION.md](docs/MQTT_INTEGRATION.md) for setup guide.

## Configuration

Key settings in `src/config.h`:
- `FIRMWARE_VERSION` - Current version
- `DATA_PIN` - LED strip data pin (default: GPIO 4)
- `DEFAULT_BRIGHTNESS` - Initial brightness (0-255)
- `CLOCK_NAME` - Device name
- `TZ_INFO` - Timezone string

See [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) for full configuration reference.

## Development

### Building from Source
```bash
# Install dependencies
pio lib install

# Build
pio run

# Run tests
pio test -e native
```

### Contributing
We welcome contributions! Please see [CONTRIBUTING.md](docs/CONTRIBUTING.md) for:
- Code style guidelines
- Pull request process
- Development workflow

### Testing
```bash
# Unit tests (native)
pio test -e native

# Integration tests (on-device)
pio test -e esp32_test

# All tests
pio test
```

## Troubleshooting

### Common Issues

**Clock not connecting to WiFi:**
- Hold reset for 10 seconds to clear credentials
- Connect to `Wordclock_AP` network
- Reconfigure WiFi

**LEDs not lighting:**
- Check `DATA_PIN` in config.h
- Verify power supply voltage (5V)
- Test with single LED first

**MQTT not connecting:**
- Verify broker settings in web UI
- Check network connectivity
- Review logs at http://wordclock.local/logs

For more issues, see [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Versioning

We use [Semantic Versioning](https://semver.org/):
- **Major:** Breaking changes
- **Minor:** New features (backwards compatible)
- **Patch:** Bug fixes

See [CHANGELOG.md](docs/CHANGELOG.md) for release history.

## Support & Contributions

**Getting Help:**
- 🐛 [Report a bug](https://github.com/lumetric-io/Wordclock/issues/new?template=bug_report.md)
- 💡 [Request a feature](https://github.com/lumetric-io/Wordclock/issues/new?template=feature_request.md)
- ❓ [Ask a question](https://github.com/lumetric-io/Wordclock/discussions)

**Contributing:**
Bug reports and feature requests are welcome in the issue tracker. Please include device details, firmware version, and logs from `/logs.html` where possible. Pull requests should target the `main` branch and include relevant tests or reproduction steps.

See [CONTRIBUTING.md](docs/CONTRIBUTING.md) for detailed guidelines.

## Acknowledgments

- ESP32 Arduino Core
- WiFiManager by tzapu
- ArduinoJson by Benoit Blanchon
- Adafruit NeoPixel Library
- Home Assistant Community

## License

To be determined. Until then, assume the code is proprietary to the project maintainers.

---

**Made with ❤️ for the DIY community**
