#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include "config.h"

struct SensorData {
    float temperature;
    float humidity;
    float soilMoisture;            // rata-rata probe yang valid (%)
    float soilPct[SOIL_COUNT];     // per probe (%), NAN bila probe tidak masuk akal
    float soilRaw[SOIL_COUNT];     // ADC mentah per probe (untuk kalibrasi)
    bool  valid;        // DHT22 terbaca & suhu/RH masuk rentang
    bool  soilValid;    // minimal satu probe tanah dalam rentang raw yang masuk akal
    // Status per probe. soilPct[]/soilRaw[] berisi NAN saat probe gagal, dan
    // field itu dibuang dari JSON (putNum) — jadi dashboard tidak bisa
    // membedakan "probe tercabut" dari "record firmware lama". Dua angka ini
    // dikirim eksplisit supaya bedanya jelas.
    uint8_t soilOk;     // bitmask probe valid: bit0=SOIL_PINS[0], bit1=..[1], dst
    uint8_t soilCount;  // jumlah probe valid (0..SOIL_COUNT)
};

void initSensor();
SensorData readSensors();
const char* getSensorError();

// Bus I2C LCD (0x27) dipakai dua task; Wire tidak thread-safe. Setiap transaksi
// LCD WAJIB lewat i2cLock()/i2cUnlock() dengan timeout pendek, jangan menunggu
// tanpa batas. DHT22 berada di GPIO terpisah dan tidak memakai bus I2C.
void i2cBusInit();                       // sekali di setup(), sebelum initLcd()
bool i2cLock(TickType_t timeout);
void i2cUnlock();

#endif
