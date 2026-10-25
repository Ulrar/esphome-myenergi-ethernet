# MyEnergi Ethernet for ESPHome

An early ESPHome external component for passively reading telemetry broadcast by MyEnergi Zappi chargers on a local Ethernet network. The observed frames use EtherType `0x88b5`; they are not IP packets. The component does not send frames or change charger settings.

## Current field mappings

These mappings are based on packet captures correlated with Home Assistant readings. They describe the observed record variants, not a complete protocol specification.

| Record | Field | Decode |
| --- | --- | --- |
| `01 36` | Grid power | Signed little-endian 32-bit watts at payload offsets 30–33. |
| `10 35` with subrecord `20 1d` | Zappi charging power | Little-endian 16-bit watts at offsets 38–39. The device serial is at offsets 30–33. |
| `10 35` with subrecord `1f 1d` | Session energy | Little-endian 16-bit value at offsets 44–45, divided by 100 for kWh. |
| `10 35` with subrecord `32 1d` or `35 1d` | PWM | Little-endian 16-bit value at offsets 52–53, divided by 100 for percent. |
| `3e 1d 5a 5a` | Voltage and frequency candidates | Voltage-like values at offsets 60, 62, and 64 divided by 10; frequency-like value at offset 66 divided by 100. Device serial is at offsets 82–85. These entities are provisional and disabled by default. |

Offsets are relative to the frame payload after the Ethernet header. Charger serials in records are little-endian. The component matches per-charger records by serial rather than source MAC address.

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
```

Replace the serial placeholder with the charger's decimal serial number; do not copy the placeholder literally. Add another entry under `devices:` for each charger. The device `name` is used as a prefix, followed by each measurement name. Omit it to use `Zappi <serial>`. Each sensor can still set its own `name` to override the generated one.

## Switch setup

The frames are Layer 2 multicast. They may reach other ports in the same VLAN through normal multicast forwarding, so a mirror session may not be necessary. Put the ESP32's Ethernet port on the chargers' VLAN and check for EtherType `0x88b5` traffic. If the switch filters this multicast, mirror a charger or hub port to the ESP32 port instead. IGMP snooping applies to IP multicast groups and does not configure forwarding for these non-IP frames.

When the Ethernet port is a normal switch port on the LAN, ESPHome can use it for both telemetry and Home Assistant connectivity. A mirror destination port may be monitor-only; check the switch behavior before relying on it for network access.

## Implementation status and limitations

This is a prototype and has not yet been compiled against a specific ESPHome release or ESP32 Ethernet board. It relies on ESP-IDF's Ethernet receive-path hook and promiscuous mode. Validate those APIs and frame delivery on the target before using the sensors for long-term logging.

Grid power is exposed as a shared reading. The voltage channels are not fully identified. Other record families, checksums, version differences, and charger control commands are not decoded.

Do not commit packet captures or installation-specific serial numbers, MAC addresses, IP addresses, or timestamps. Captures can contain those details even when the decoded fields do not.
