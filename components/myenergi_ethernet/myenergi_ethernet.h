#pragma once

#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ethernet/ethernet_component.h"
#include "esphome/components/sensor/sensor.h"

#include <cstdint>
#include <vector>

#include "esp_eth.h"
#include "esp_netif.h"

namespace esphome {
namespace myenergi_ethernet {

struct CtChannelSensors {
  uint8_t channel{0};
  sensor::Sensor *power{nullptr};
  sensor::Sensor *current{nullptr};
};

struct ZappiSensors {
  uint32_t serial{0};
  sensor::Sensor *charging_power{nullptr};
  sensor::Sensor *session_energy{nullptr};
  sensor::Sensor *pwm{nullptr};
  sensor::Sensor *voltage_candidate_1{nullptr};
  sensor::Sensor *voltage_candidate_2{nullptr};
  sensor::Sensor *voltage_candidate_3{nullptr};
  sensor::Sensor *grid_frequency_candidate{nullptr};
  sensor::Sensor *mode_code_candidate{nullptr};
  binary_sensor::BinarySensor *plugged_in_candidate{nullptr};
  std::vector<CtChannelSensors> ct_channels;
};

class MyEnergiEthernetComponent : public Component {
 public:
  void set_ethernet_component(ethernet::EthernetComponent *ethernet) { ethernet_ = ethernet; }
  void set_grid_power_sensor(sensor::Sensor *sensor) { grid_power_ = sensor; }

  void set_charging_power_sensor(uint32_t serial, sensor::Sensor *sensor);
  void set_session_energy_sensor(uint32_t serial, sensor::Sensor *sensor);
  void set_pwm_sensor(uint32_t serial, sensor::Sensor *sensor);
  void set_voltage_candidate_1_sensor(uint32_t serial, sensor::Sensor *sensor);
  void set_voltage_candidate_2_sensor(uint32_t serial, sensor::Sensor *sensor);
  void set_voltage_candidate_3_sensor(uint32_t serial, sensor::Sensor *sensor);
  void set_grid_frequency_candidate_sensor(uint32_t serial, sensor::Sensor *sensor);
  void set_mode_code_candidate_sensor(uint32_t serial, sensor::Sensor *sensor);
  void set_plugged_in_candidate_binary_sensor(uint32_t serial, binary_sensor::BinarySensor *sensor);
  void set_ct_channel_power_sensor(uint32_t serial, uint8_t channel, sensor::Sensor *sensor);
  void set_ct_channel_current_sensor(uint32_t serial, uint8_t channel, sensor::Sensor *sensor);

  void setup() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

 protected:
  static esp_err_t input_callback(esp_eth_handle_t handle, uint8_t *buffer, uint32_t length,
                                  void *context);
  esp_err_t handle_frame(const uint8_t *frame, size_t length);
  void handle_record_(const uint8_t *record, size_t length);
  ZappiSensors &get_or_create_device_(uint32_t serial);
  ZappiSensors *find_device_(uint32_t serial);

  ethernet::EthernetComponent *ethernet_{nullptr};
  esp_eth_handle_t eth_handle_{nullptr};
  esp_netif_t *netif_{nullptr};
  sensor::Sensor *grid_power_{nullptr};
  std::vector<ZappiSensors> devices_;
};

}  // namespace myenergi_ethernet
}  // namespace esphome
