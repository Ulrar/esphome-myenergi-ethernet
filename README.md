# MyEnergi Ethernet for ESPHome

An early ESPHome external component for passively reading telemetry broadcast by MyEnergi Zappi chargers on a local Ethernet network. The observed frames use EtherType `0x88b5`; they are not IP packets. The component does not send frames or change charger settings.

## Current field mappings

These mappings are based on packet captures correlated with Home Assistant readings. The `0x88b5` payload has a 24-byte envelope followed by length-prefixed records. This describes the observed record variants, not a complete protocol specification.

| Record | Field | Decode |
| --- | --- | --- |
| Type `0x3601` | Grid power | Signed little-endian 32-bit watts at record offsets 6–9 (frame payload offsets 30–33). |
| Type `0x3510`, length `0x20` | Zappi charging power | Little-endian 16-bit watts at record offsets 14–15. The device serial is at record offsets 6–9. |
| Type `0x3510`, length `0x1f` | Session energy | Little-endian 16-bit value at record offsets 20–21, divided by 100 for kWh. |
| Type `0x3510`, length `0x32` or `0x35` | PWM | Little-endian 16-bit value at record offsets 28–29, divided by 100 for percent. |
| Type `0x3510`, byte 23 | Mode code candidate | Low two bits; observed as `0` while stopped and `1` while Fast charging in the current capture. Eco and Eco+ values have not been checked on this firmware. |
| Type `0x3510`, byte 12 | Plugged-in candidate | Bit 0 was `0` before a plug-in, `1` while connected, and `0` after unplugging in one capture. This is a single-cycle correlation and remains optional and disabled by default. |
| Type `0x3510`, CT subrecords | CT power and current candidates | Eight-byte subrecords carry a channel number and two signed little-endian 16-bit values. Value A correlates with watts on the charger-load channel. Value B divided by 100 correlates with amps: on the charging-load channel, power divided by this current agrees with the independently reported line voltage. The scale has not been checked across all CT roles, so both entities remain optional candidates. |
| Type `0x5a5a`, length `0x3e` | Voltage and frequency candidates | Voltage-like values at record offsets 36, 38, and 40 divided by 10; frequency-like value at offset 42 divided by 100. Device serial is at record offsets 58–61. These entities are provisional and disabled by default. |

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
      # Candidate entities are optional and disabled by default.
      voltage_candidate_1: {}
      grid_frequency_candidate: {}
      mode_code_candidate: {}
      plugged_in_candidate: {}
      ct_channels:
        - channel: 1
          power_candidate: {}
          current_candidate: {}
```

Replace the serial placeholder with the charger's decimal serial number; do not copy the placeholder literally. Add another entry under `devices:` for each charger. The device `name` is used as a prefix, followed by each measurement name. Omit it to use `Zappi <serial>`. Each sensor can still set its own `name` to override the generated one.

## Switch setup

The frames are Layer 2 multicast. They may reach other ports in the same VLAN through normal multicast forwarding, so a mirror session may not be necessary. Put the ESP32's Ethernet port on the chargers' VLAN and check for EtherType `0x88b5` traffic. If the switch filters this multicast, mirror a charger or hub port to the ESP32 port instead. IGMP snooping applies to IP multicast groups and does not configure forwarding for these non-IP frames.

When the Ethernet port is a normal switch port on the LAN, ESPHome can use it for both telemetry and Home Assistant connectivity. A mirror destination port may be monitor-only; check the switch behavior before relying on it for network access.

## Implementation status and limitations

This is a prototype and has not yet been compiled against a specific ESPHome release or ESP32 Ethernet board. It relies on ESP-IDF's Ethernet receive-path hook and promiscuous mode. Validate those APIs and frame delivery on the target before using the sensors for long-term logging.

Grid power is exposed as a shared reading. CT channel power/current, the mode code, and plugged-in state are optional diagnostic candidates; the charger-load CT power/current, Stop/Fast mode codes, and one plug/unplug transition have been correlated on this firmware. The plug-state bit needs confirmation with more cycles and devices. The meaning of other CT channels, the `0x7979` record, voltage phase assignments, checksums, and version differences remain under investigation. Charger control commands are not decoded.

Do not commit packet captures or installation-specific serial numbers, MAC addresses, IP addresses, or timestamps. Captures can contain those details even when the decoded fields do not.
