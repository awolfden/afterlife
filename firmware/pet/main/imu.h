#pragma once
#include <M5GFX.h>
#include <cstring>
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orientation.h"
extern "C" {
#include "bmi270.h"
}
namespace watcher_orientation {
// LGFX's per-port mutex serializes IMU transactions with the feedback owner.
// A 24-byte read adds nominal ~3ms bus occupancy at 100kHz, NOT a worst-case
// motor-stop bound. Only the feedback owner still writes codec/IOE registers.
// Never installs a second I2C driver or uses NVS.
class Imu {
 bmi2_dev device_{};Settled settled_;bool ready_=false;unsigned errors_=0;int64_t read_at_=-1000;
 static int8_t read(uint8_t reg,uint8_t* data,uint32_t size,void*){
  if(!data||!size||size>32)return -1;
  return lgfx::i2c::readRegister(1,0x68,reg,data,size,100000).has_error()?-1:0;
 }
 static int8_t write(uint8_t reg,const uint8_t* data,uint32_t size,void*){
  if(!data||!size||size>32)return -1;
  uint8_t bytes[33];bytes[0]=reg;std::memcpy(bytes+1,data,size);
  return lgfx::i2c::transactionWrite(1,0x68,bytes,size+1,100000).has_error()?-1:0;
 }
 static void delay(uint32_t us,void*){
  const uint32_t ticks=us/(portTICK_PERIOD_MS*1000u);
  if(ticks)vTaskDelay(ticks+1); // One-tick margin: a tick wait can begin just before a boundary.
  const uint32_t remainder=us%(portTICK_PERIOD_MS*1000u);if(remainder)esp_rom_delay_us(remainder);
 }
 public:
 bool init(){
  device_.read=read;device_.write=write;device_.delay_us=delay;device_.intf=BMI2_I2C_INTF;
  device_.intf_ptr=this;device_.read_write_len=30;
  if(bmi270_init(&device_)!=BMI2_OK)return false;
  bmi2_sens_config config{};config.type=BMI2_ACCEL;
  if(bmi2_get_sensor_config(&config,1,&device_)!=BMI2_OK)return false;
  config.cfg.acc.bwp=BMI2_ACC_NORMAL_AVG4;config.cfg.acc.odr=BMI2_ACC_ODR_50HZ;
  config.cfg.acc.filter_perf=BMI2_PERF_OPT_MODE;config.cfg.acc.range=BMI2_ACC_RANGE_4G;
  if(bmi2_set_sensor_config(&config,1,&device_)!=BMI2_OK)return false;
  const uint8_t enable=BMI2_ACCEL;
  ready_=bmi2_sensor_enable(&enable,1,&device_)==BMI2_OK;return ready_;
 }
 bool ready() const{return ready_;}
 unsigned update(int64_t now){
  if(!ready_||now-read_at_<50)return settled_.current();
  read_at_=now;
  bmi2_sens_data data{};
  if(bmi2_get_sensor_data(&data,&device_)!=BMI2_OK){settled_.unavailable();if(++errors_>=3)ready_=false;return settled_.current();}
  errors_=0;
  // Factory StopWatch mapping swaps sensor X/Y. Physical orientation acceptance remains required.
  return settled_.update(data.acc.y/8192.0f,data.acc.x/8192.0f,data.acc.z/8192.0f,now);
 }
};
}
