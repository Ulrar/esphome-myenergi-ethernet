#include "myenergi_ethernet.h"

#include "esphome/core/log.h"

#include <cstring>

namespace esphome {
namespace myenergi_ethernet {

static const char *const TAG = "myenergi_ethernet";

namespace {

uint16_t read_u16_le(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t read_u32_le(const uint8_t *data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

int32_t read_i32_le(const uint8_t *data) { return static_cast<int32_t>(read_u32_le(data)); }

}  // namespace

void MyEnergiEthernetComponent::setup() {
  if (this->ethernet_ == nullptr) {
    ESP_LOGE(TAG, "No ESPHome Ethernet component was configured");
    this->mark_failed();
    return;
  }

  this->eth_handle_ = this->ethernet_->get_eth_handle();
  this->netif_ = this->ethernet_->get_esp_netif();
  if (this->eth_handle_ == nullptr || this->netif_ == nullptr) {
    ESP_LOGE(TAG, "Ethernet driver is not ready; check ethernet_id and setup order");
    this->mark_failed();
    return;
  }

  // The observed Zappi frames are multicast. Promiscuous mode lets the ESP
  // receive them even when the Ethernet MAC has not joined that group.
  bool promiscuous = true;
  esp_err_t err = esp_eth_ioctl(this->eth_handle_, ETH_CMD_S_PROMISCUOUS, &promiscuous);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not enable Ethernet promiscuous mode: %s", esp_err_to_name(err));
  }

  // Tap raw frames before lwIP, then pass them to the regular netif path so
  // the ESPHome Ethernet interface continues to behave normally.
  err = esp_eth_update_input_path(this->eth_handle_, &MyEnergiEthernetComponent::input_callback, this);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Could not install Ethernet receive hook: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Listening for MyEnergi 0x88b5 Ethernet frames");
}

esp_err_t MyEnergiEthernetComponent::input_callback(esp_eth_handle_t, uint8_t *buffer, uint32_t length,
                                                    void *context) {
  auto *component = static_cast<MyEnergiEthernetComponent *>(context);
  if (component == nullptr || buffer == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  component->handle_frame(buffer, length);
  // Keep the normal ESPHome Ethernet/IP path intact for frames that belong to
  // this interface. MyEnergi frames are inspected above and ignored by IP.
  return esp_netif_receive(component->netif_, buffer, length, nullptr);
}

esp_err_t MyEnergiEthernetComponent::handle_frame(const uint8_t *frame, size_t length) {
  if (length < 14) {
    return ESP_OK;
  }

  size_t ethernet_header_length = 14;
  uint16_t ether_type = (static_cast<uint16_t>(frame[12]) << 8) | frame[13];
  if (ether_type == 0x8100 || ether_type == 0x88A8) {
    if (length < 18) {
      return ESP_OK;
    }
    ether_type = (static_cast<uint16_t>(frame[16]) << 8) | frame[17];
    ethernet_header_length = 18;
  }
  if (ether_type != 0x88B5 || length < ethernet_header_length) {
    return ESP_OK;
  }

  const uint8_t *payload = frame + ethernet_header_length;
  const size_t payload_length = length - ethernet_header_length;
  if (payload_length < 28 || payload[0] != 0xCB || payload[1] != 0xDA || payload[2] != 0xE9 ||
      payload[3] != 0xF8 || payload[20] != 0x01 || payload[21] != 0x00 || payload[22] != 0x00 ||
      payload[23] != 0x00) {
    return ESP_OK;
  }

  const uint8_t record_0 = payload[24];
  const uint8_t record_1 = payload[25];
  const uint8_t marker_0 = payload[26];
  const uint8_t marker_1 = payload[27];

  // Grid power: record 01 36, signed little-endian watts at payload[30:34].
  if (record_0 == 0x01 && record_1 == 0x36 && payload_length >= 34 && this->grid_power_ != nullptr) {
    this->grid_power_->publish_state(static_cast<float>(read_i32_le(payload + 30)));
    return ESP_OK;
  }

  if (marker_0 == 0x10 && marker_1 == 0x35 && payload_length >= 54) {
    const uint32_t serial = read_u32_le(payload + 30);
    ZappiSensors *device = this->find_device_(serial);
    if (device == nullptr) {
      return ESP_OK;
    }

    if (record_0 == 0x20 && record_1 == 0x1D && device->charging_power != nullptr) {
      device->charging_power->publish_state(static_cast<float>(read_u16_le(payload + 38)));
    } else if (record_0 == 0x1F && record_1 == 0x1D && device->session_energy != nullptr) {
      device->session_energy->publish_state(static_cast<float>(read_u16_le(payload + 44)) / 100.0f);
    } else if ((record_0 == 0x32 || record_0 == 0x35) && record_1 == 0x1D && device->pwm != nullptr) {
      device->pwm->publish_state(static_cast<float>(read_u16_le(payload + 52)) / 100.0f);
    }
    return ESP_OK;
  }

  // Voltage/frequency values are useful candidates, but their exact channel
  // meanings are still being checked against the cloud/HA readings.
  if (record_0 == 0x3E && record_1 == 0x1D && marker_0 == 0x5A && marker_1 == 0x5A &&
      payload_length >= 86) {
    const uint32_t serial = read_u32_le(payload + 82);
    ZappiSensors *device = this->find_device_(serial);
    if (device == nullptr) {
      return ESP_OK;
    }
    if (device->voltage_candidate_1 != nullptr) {
      device->voltage_candidate_1->publish_state(static_cast<float>(read_u16_le(payload + 60)) / 10.0f);
    }
    if (device->voltage_candidate_2 != nullptr) {
      device->voltage_candidate_2->publish_state(static_cast<float>(read_u16_le(payload + 62)) / 10.0f);
    }
    if (device->voltage_candidate_3 != nullptr) {
      device->voltage_candidate_3->publish_state(static_cast<float>(read_u16_le(payload + 64)) / 10.0f);
    }
    if (device->grid_frequency_candidate != nullptr) {
      device->grid_frequency_candidate->publish_state(static_cast<float>(read_u16_le(payload + 66)) / 100.0f);
    }
  }

  return ESP_OK;
}

ZappiSensors &MyEnergiEthernetComponent::get_or_create_device_(uint32_t serial) {
  for (auto &device : this->devices_) {
    if (device.serial == serial) {
      return device;
    }
  }
  this->devices_.push_back(ZappiSensors{});
  this->devices_.back().serial = serial;
  return this->devices_.back();
}

ZappiSensors *MyEnergiEthernetComponent::find_device_(uint32_t serial) {
  for (auto &device : this->devices_) {
    if (device.serial == serial) {
      return &device;
    }
  }
  return nullptr;
}

void MyEnergiEthernetComponent::set_charging_power_sensor(uint32_t serial, sensor::Sensor *sensor) {
  this->get_or_create_device_(serial).charging_power = sensor;
}

void MyEnergiEthernetComponent::set_session_energy_sensor(uint32_t serial, sensor::Sensor *sensor) {
  this->get_or_create_device_(serial).session_energy = sensor;
}

void MyEnergiEthernetComponent::set_pwm_sensor(uint32_t serial, sensor::Sensor *sensor) {
  this->get_or_create_device_(serial).pwm = sensor;
}

void MyEnergiEthernetComponent::set_voltage_candidate_1_sensor(uint32_t serial, sensor::Sensor *sensor) {
  this->get_or_create_device_(serial).voltage_candidate_1 = sensor;
}

void MyEnergiEthernetComponent::set_voltage_candidate_2_sensor(uint32_t serial, sensor::Sensor *sensor) {
  this->get_or_create_device_(serial).voltage_candidate_2 = sensor;
}

void MyEnergiEthernetComponent::set_voltage_candidate_3_sensor(uint32_t serial, sensor::Sensor *sensor) {
  this->get_or_create_device_(serial).voltage_candidate_3 = sensor;
}

void MyEnergiEthernetComponent::set_grid_frequency_candidate_sensor(uint32_t serial, sensor::Sensor *sensor) {
  this->get_or_create_device_(serial).grid_frequency_candidate = sensor;
}

}  // namespace myenergi_ethernet
}  // namespace esphome
