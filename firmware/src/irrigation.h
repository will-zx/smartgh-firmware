#ifndef IRRIGATION_H
#define IRRIGATION_H

#include <Arduino.h>
#include <stdint.h>

// Empat pompa, satu pompa aktif pada satu waktu.
enum IrrigPhase { IRRIG_IDLE, IRRIG_FILL, IRRIG_DOSE_A, IRRIG_DOSE_B, IRRIG_SETTLE, IRRIG_DIST };

// Nama fase ("IDLE", "DIST") untuk log serial dan status_sistem.
const char* irrigationPhaseName();

// Status irigasi untuk telemetri (dashboard) & log.
struct IrrigStatus {
    const char*   phaseName;
    const char*   result;
    float         phaseTargetMl;
    unsigned long phaseDurationMs;
    float         phaseEstimatedMl;
    float         doseAMl;
    float         doseBMl;
    unsigned long fillDurationMs;
    unsigned long doseADurationMs;
    unsigned long doseBDurationMs;
    unsigned long distDurationMs;
    float         doseMl;          // dosis per siklus (mL)
    unsigned long doseMs;          // durasi fase DIST (ms)
    float         budgetMl;        // anggaran harian (mL)
    float         usedMl;          // volume yang sudah dialirkan hari ini (mL)
    unsigned long minIntervalMs;   // jeda minimum antar siklus (ms)
    const char*   holdReason;      // alasan permintaan ditahan ("" = tidak ditahan)
};

// Kumpulkan sampel suhu tersaring untuk Tmean harian 24 jam.
// Dipanggil tiap iterasi controlTask dengan suhu yang SUDAH tervalidasi.
void irrigationFeedTemp(float temp);

// Tmean = rerata 24 jam yang sah; NaN sebelum jendela penuh.
float irrigationTmean();

// ETc (mm/hari) = Kc(fase) * p(bulan) * (0,46*Tmean + 8), dengan p dari
// Tabel FAO-24 untuk lintang Semarang (SUN_P_MONTH di config.h).
// Pendekatan praktis Blaney-Criddle, BUKAN ETo standar FAO-56.
float calculateEtc();

// Dosis irigasi dari ETc:
//   anggaran_harian_ml = ETc * IRRIG_AREA_CM2 * 0,1   (1 mm x 1 cm2 = 0,1 mL)
//   dosis_ml           = anggaran_harian_ml / IRRIG_CYCLES_PER_DAY
//   durasi_pompa_ms    = target_ml / DIST_FLOW_ML_S * 1000
//   jeda_minimum_ms    = 24 jam / IRRIG_CYCLES_PER_DAY
float irrigationDailyBudgetMl();
float irrigationDoseMl();
unsigned long irrigationDoseMs();
unsigned long irrigationMinIntervalMs();
IrrigStatus irrigationStatus();

// Memproses permintaan irigasi. Siklus baru dimulai HANYA bila: trigger true
// DAN jeda minimum sudah lewat DAN (terkumpul + dosis <= anggaran harian).
// Permintaan yang ditahan (mis. jeda minimum belum lewat) dicoba ulang
// otomatis pada iterasi berikutnya selama trigger tetap true.
//
// manualRequestId bertambah pada setiap perintah ON eksplisit; tiap ID baru
// memulai satu siklus manual (termasuk ON berulang tanpa melepas override).
void irrigationUpdate(bool trigger, bool manual, uint32_t manualRequestId);
bool irrigationRunning();         // ada fase aktif (dipakai status_sistem)
void irrigationAbort();           // sensor tidak valid / darurat -> pompa mati

#endif
