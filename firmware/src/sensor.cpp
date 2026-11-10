#include "sensor.h"
#include <DHT.h>

static DHT dht(DHT_PIN, DHT22);
static bool dhtReadOk = false;

// Mutex bus I2C LCD: Wire tidak thread-safe dan bus dipakai dua task
// (control + scroll LCD), jadi semua transaksi LCD lewat sini.
static SemaphoreHandle_t i2cMux = nullptr;

void i2cBusInit() {
    if (!i2cMux) i2cMux = xSemaphoreCreateMutex();
}

bool i2cLock(TickType_t timeout) {
    // Belum di-init = masih fase setup satu task; tidak perlu dikunci.
    if (!i2cMux) return true;
    return xSemaphoreTake(i2cMux, timeout) == pdTRUE;
}

void i2cUnlock() {
    if (i2cMux) xSemaphoreGive(i2cMux);
}

// Salinan SOIL_PINS / SOIL_*_CAL dari config.h. Array (bukan #define langsung
// di dalam loop) supaya satu loop bisa menangani tiga probe tanpa duplikasi.
static const uint8_t soilPins[SOIL_COUNT] = SOIL_PINS;
static const int     soilDry[SOIL_COUNT]  = SOIL_DRY_CAL;
static const int     soilWet[SOIL_COUNT]  = SOIL_WET_CAL;

void initSensor() {
    dht.begin();
    Serial.printf("[sensor] DHT22 mulai pada GPIO %u\n", DHT_PIN);
    for (uint8_t ch = 0; ch < SOIL_COUNT; ch++) {
        pinMode(soilPins[ch], INPUT);
    }
}

// Rata-rata SOIL_SAMPLES bacaan satu kanal; sampel pertama dibuang karena ADC
// masih membawa sisa muatan kanal sebelumnya (lihat SOIL_SAMPLES di config.h).
static float readSoilRaw(uint8_t ch) {
    analogRead(soilPins[ch]);
    float sum = 0.0f;
    for (int s = 0; s < SOIL_SAMPLES; s++) sum += (float)analogRead(soilPins[ch]);
    return sum / (float)SOIL_SAMPLES;
}

// Raw -> persen untuk satu probe. false = angka tidak masuk akal (probe
// dicabut / kabel putus / salah pin): ADC mengambang 0 atau 4095, di luar
// rentang kalibrasi, sehingga 0 %/100 % palsu tidak boleh dipakai keputusan.
static bool soilPercent(uint8_t ch, float raw, float* out) {
    float lo = (float)soilWet[ch] - (float)SOIL_RAW_TOL;
    float hi = (float)soilDry[ch] + (float)SOIL_RAW_TOL;
    if (isnan(raw) || raw < lo || raw > hi) return false;
    // ADC terbalik: kering = raw tinggi. constrain dulu supaya map tidak
    // menghasilkan negatif/>100 saat raw sedikit melewati titik kalibrasi.
    float clamped = constrain(raw, (float)soilWet[ch], (float)soilDry[ch]);
    *out = (float)map((long)clamped, soilDry[ch], soilWet[ch], 0, 100);
    return true;
}

SensorData readSensors() {
    SensorData data;
    data.temperature = dht.readTemperature();
    data.humidity = dht.readHumidity();
    dhtReadOk = isfinite(data.temperature) && isfinite(data.humidity) &&
                data.temperature > -20.0f && data.temperature < 60.0f &&
                data.humidity >= 0.0f && data.humidity <= 100.0f;

    // Tiga probe dibaca bergantian. Keputusan irigasi memakai RATA-RATA probe
    // yang valid, sedangkan tiap probe tetap dilaporkan (soil2/soil3) supaya
    // pot/polybag yang mengering lebih dulu kelihatan di dashboard.
    float   sum = 0.0f;
    uint8_t n = 0;
    uint8_t okMask = 0;
    for (uint8_t ch = 0; ch < SOIL_COUNT; ch++) {
        float raw = readSoilRaw(ch);
        float pct = NAN;
        data.soilRaw[ch] = raw;
        if (soilPercent(ch, raw, &pct)) {
            sum += pct;
            okMask |= (uint8_t)(1u << ch);   // bit per kanal, urut SOIL_PINS
            n++;
        }
        data.soilPct[ch] = pct;
        if (ch + 1 < SOIL_COUNT) delay(SOIL_SETTLE_MS);
    }
    data.soilOk = okMask;
    data.soilCount = n;
    data.soilValid = (n > 0);
    data.soilMoisture = (n > 0) ? (sum / (float)n) : 0.0f;

    data.valid = dhtReadOk;
    return data;
}

const char* getSensorError() {
    return dhtReadOk ? "OK" : "DHT22 tidak terbaca / data di luar rentang";
}
