# MyEnergi Ethernet for ESPHome

An early ESPHome external component for passively reading telemetry broadcast by MyEnergi Zappi chargers on a local Ethernet network. The observed frames use EtherType `0x88b5`; they are not IP packets. The component does not send frames or change charger settings.

## Current field mappings

These mappings are based on packet captures correlated with Home Assistant readings. The `0x88b5` payload has a 24-byte envelope followed by length-prefixed records. This describes the observed record variants, not a complete protocol specification.

| Record | Field | Decode and validation |
| --- | --- | --- |
| Type `0x3601` | Grid power | Signed little-endian 32-bit watts at record offsets 6–9 (frame payload offsets 30–33). Confirmed against the household grid reading. |
| Type `0x3510`, length `0x20` | Zappi charging power | Little-endian 16-bit watts at record offsets 14–15. The device serial is at record offsets 6–9. Confirmed during a charge against the app and cloud readings. |
| Type `0x3510`, length `0x1f` | Session energy | Little-endian 16-bit value at record offsets 20–21, divided by 100 for kWh. Confirmed during a full session; the final 50.88 kWh matched the cloud session total. This value is per session and resets for a later session. |
| Type `0x3510`, length `0x27`, offset 30 | Firmware-code candidate | Little-endian 16-bit word was `5418` (`0x152a`) in some records from a Zappi running 5.418; the following word at offset 32 was `3560`, matching the cloud identifier `3560S5.418`. Before upgrade, the same field was `0x2000` in 17 observed records, with no `5417` value. After upgrade and reboot, `5418` appeared twice, then the field returned to `0x2000`. This is a strong but intermittent candidate; the 5.417 encoding and meaning of `0x2000` are unknown. |
| Type `0x3510`, length `0x32` or `0x35` | PWM | Little-endian 16-bit value at record offsets 28–29, divided by 100 for percent. |
| Type `0x3510`, byte 23 | Mode code | Low two bits: `0` = Stop, `1` = Fast, `2` = Eco, `3` = Eco+. All four were confirmed against live ESPHome readings on a Zappi running 5.418; Stop and Fast were also observed on 5.417. |
| Type `0x3510`, byte 12 | Plug-state bit (unresolved) | Bit 0 previously correlated with unplugged/connected states. During an overnight charge it went low as Fast mode began, stayed low after charging completed, and returned high after switching to Stop while HA reported EV Connected. It may represent readiness or another charger state; do not treat it as a reliable plug detector yet. |
| Type `0x3510`, CT subrecords | CT channel power and current | Eight-byte subrecords carry a channel number and two signed little-endian 16-bit values. Value A is watts; value B divided by 100 is amps. The charger-load channel's power and current were confirmed during charging, and their ratio agreed with the measured line voltage. Other CT channel roles still need validation. |
| Type `0x5a5a`, length `0x3e` | Grid voltage and frequency | Voltage values at record offsets 36, 38, and 40 divided by 10; frequency at offset 42 divided by 100. Device serial is at record offsets 58–61. The first voltage and frequency matched the single-phase installation's cloud readings; voltage channels 2 and 3 remain candidates. |

Record offsets are relative to each length-prefixed record; frame-payload offsets are relative to the bytes after the Ethernet header. Charger serials in records are little-endian. The component matches per-charger records by serial rather than source MAC address.

## Configuration

Use an ESP32 board with Ethernet support and the ESP-IDF framework. Configure the board's Ethernet PHY and pins using its normal ESPHome `ethernet:` settings. The component uses that interface both to inspect MyEnergi frames and to carry ESPHome network traffic.

Add the component to your ESPHome configuration:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/Ulrar/esphome-myenergi-ethernet
      ref: main
    components: [myenergi_ethernet]

myenergi_ethernet:
  ethernet_id: wired_lan
  grid_power:
    name: Grid power
  devices:
    - serial: YOUR_ZAPPI_SERIAL # Replace with the decimal serial from the Zappi telemetry.
      name: Garage Zappi # Optional; defaults to "Zappi <serial>".
      charging_power: {}
      session_energy: {}
      pwm: {}
      # Optional diagnostic entities (disabled by default). The plug-state bit remains unresolved.
      grid_voltage: {}
      grid_frequency: {}
      mode_code: {}
      plugged_in_candidate: {}
      ct_channels:
        - channel: 1
          power: {}
          current: {}
```

Replace the serial placeholder with the charger's decimal serial number; do not copy the placeholder literally. Add another entry under `devices:` for each charger. The device `name` is used as a prefix, followed by each measurement name. Omit it to use `Zappi <serial>`. Each sensor can still set its own `name` to override the generated one.

## Switch setup

The frames are Layer 2 multicast. They may reach other ports in the same VLAN through normal multicast forwarding, so a mirror session may not be necessary. Put the ESP32's Ethernet port on the chargers' VLAN and check for EtherType `0x88b5` traffic. If the switch filters this multicast, mirror a charger or hub port to the ESP32 port instead. IGMP snooping applies to IP multicast groups and does not configure forwarding for these non-IP frames.

When the Ethernet port is a normal switch port on the LAN, ESPHome can use it for both telemetry and Home Assistant connectivity. A mirror destination port may be monitor-only; check the switch behavior before relying on it for network access.

## Implementation status and limitations

The component has been compiled and flashed on an ESP32 with a LAN8720 Ethernet PHY using ESP-IDF. It relies on ESP-IDF's Ethernet receive-path hook and promiscuous mode; test frame delivery on the target network before relying on it for long-term logging.

The readings validated above matched the tested Zappi 2 units and firmware versions; other hardware and firmware may differ. Configuration keys for confirmed readings are `grid_voltage`, `grid_frequency`, `mode_code`, and CT channel `power` / `current`. These replace the former candidate-suffixed keys. `voltage_candidate_2`, `voltage_candidate_3`, and `plugged_in_candidate` remain provisional. The `0x3510` length `0x27` firmware-code field is an intermittent candidate: the 5.417 representation and alternate `0x2000` value remain unexplained. Other CT channel roles, the `0x7979` record, voltage phase assignments, and checksums remain under investigation. Charger control commands are not decoded.

For energy tracking, `session_energy` resets between charges and should not be used as a daily or lifetime total. In Home Assistant, convert the continuous `charging_power` reading to an accumulated kWh sensor with the Integral (Riemann sum) helper. Use that total for the Energy Dashboard; add a Utility Meter with a daily cycle if you also want a separate “today” sensor.

Do not commit packet captures or installation-specific serial numbers, MAC addresses, IP addresses, or timestamps. Captures can contain those details even when the decoded fields do not.
