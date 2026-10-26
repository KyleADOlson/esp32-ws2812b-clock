# LED Clock (ESP32-C3 SuperMini + 32x8 WS2812B)

12-hour NTP-synced clock with fixed-width 5x7 digits, a steady colon and a small A/P indicator.

- Panel DIN -> GPIO1. Panel power from its own 5V supply (tie grounds together).
- BOOT button (GPIO9): hold 3 s while running to forget Wi-Fi and reboot into the setup portal.

## Build / flash

From an ESP-IDF v5.5 shell:

```
idf.py build
idf.py -p COM3 flash
```

## Web control page

Once the clock is on your network, open **http://led-clock.local/** (or `http://<ip>/`;
`status` prints the IP). The page changes color, brightness, A/P indicator, panel layout,
time zone, NTP server, Wi-Fi and TX power, and can show the test pattern or reboot.
Changes show on the clock immediately while you adjust them; press **Save now** in the bar
that appears to store them right away, otherwise they are saved automatically a few seconds
after your last change. The page has no password, so anyone on
your network can use it. The setup portal serves the same page.

## Configure Wi-Fi (no portal needed)

Over USB, either interactively:

```
idf.py -p COM3 monitor
clock> wifi "My Network" "my password"
```

or with the helper script (uses the IDF Python env's pyserial and doesn't reset the board):

```
python tools/clockctl.py COM3 wifi "My Network" "my password"
python tools/clockctl.py COM3 status
```

If no network is saved, the clock shows `SETUP` and opens an open access point named
`LED-Clock-XXXX`. Join it with a phone; the captive portal page lets you pick the network,
enter the password and choose a time zone.

## Console commands

| Command | Purpose |
|---|---|
| `status` | Wi-Fi, IP, current time, settings |
| `wifi <ssid> [pass]` | Save network and reboot |
| `wifi_reset` | Forget network, reboot into portal |
| `tz <name or POSIX>` | `pacific`, `mountain`, `arizona`, `central`, `eastern`, `alaska`, `hawaii`, `utc`, `london`, `europe`, `india`, `japan`, `sydney`, or any POSIX TZ string |
| `brightness <1-255>` | Default 32 |
| `color <r g b>` / `color <name>` | amber (default), red, green, blue, white, cyan, purple, pink, yellow |
| `ampm on/off` | A/P indicator (off centers the time) |
| `layout <0-15>` | Panel wiring, see below |
| `test` | 20 s wiring test pattern |
| `txpower <dBm>` | Cap Wi-Fi TX power; try `8` if the SuperMini has trouble connecting |
| `ntp <host>` | NTP server (default `pool.ntp.org`, applies after reboot) |
| `reboot` | Restart |

## Panel orientation

Run `test`: red should be top-left, green top-right, blue bottom-left, white bottom-right
and "TEST" should read normally. If not, change `layout` (bits add together):

- `1` pixels run along rows instead of columns
- `2` lines all run the same direction (not zig-zag)
- `4` mirror left/right
- `8` mirror top/bottom

Most 8x32 panels are columns + zig-zag, so the answer is usually 0, 4, 8 or 12.

## License

MIT, see [LICENSE](LICENSE).
