#include "irrigation.h"
#include "config.h"
#include "phase.h"
#include <Preferences.h>
#include <time.h>

static IrrigPhase phase = IRRIG_IDLE;
static uint32_t phaseStart = 0;
static uint32_t phaseDurationMs = 0;
static float cycleWaterMl = 0.0f;
static float cycleDoseAMlPerL = 0.0f, cycleDoseBMlPerL = 0.0f;
static float usedMl = 0.0f;
static uint32_t usedDay = 0;
static bool budgetLoaded = false;
static uint32_t lastManualRequestId = 0;
static char holdReason[48] = "";
static const char* processResult = "IDLE";
static Preferences prefs;

// One-minute buckets retain valid, filtered SHT31 values; only a full 24 h
// window is accepted for the daily Blaney-Criddle estimate.
static float temperatures[BC_TMEAN_BUCKETS];
static float temperatureSum = 0;
static uint16_t temperatureCount = 0;
static uint32_t temperatureBucket = UINT32_MAX;
static bool temperatureInit = false;
static uint32_t lastCycleEpoch = 0;
static char lastPrintedHold[sizeof(holdReason)] = "";

static void setPump(uint8_t pin, bool on) {
    digitalWrite(pin, (on ^ MOSFET_INVERTED) ? HIGH : LOW);
}
static void allPumpsOff() {
    setPump(PUMP_FILL, false); setPump(PUMP_DOSE_A, false);
    setPump(PUMP_DOSE_B, false); setPump(PUMP_DIST, false);
}
static float flowRate(IrrigPhase p) {
    switch (p) {
        case IRRIG_FILL: return FILL_FLOW_ML_S;
        case IRRIG_DOSE_A: return DOSE_A_FLOW_ML_S;
        case IRRIG_DOSE_B: return DOSE_B_FLOW_ML_S;
        case IRRIG_DIST: return DIST_FLOW_ML_S;
        default: return 0;
    }
}
static float durationSeconds(float targetMl, float flowMlPerSec) {
    if (!isfinite(targetMl) || !isfinite(flowMlPerSec) || targetMl < 0 || flowMlPerSec <= 0) return NAN;
    return targetMl / flowMlPerSec;
}
static unsigned long durationMs(float targetMl, float flowMlPerSec) {
    float ms = durationSeconds(targetMl, flowMlPerSec) * 1000.0f;
    return isfinite(ms) && ms >= 0 ? (unsigned long)ceilf(ms) : 0;
}
static float finalMixVolumeMl() {
    return cycleWaterMl + cycleWaterMl / 1000.0f * (cycleDoseAMlPerL + cycleDoseBMlPerL);
}
static float phaseVolume(IrrigPhase p) {
    if (p == IRRIG_FILL) return cycleWaterMl;
    if (p == IRRIG_DIST) return finalMixVolumeMl();
    if (p == IRRIG_DOSE_A) return cycleWaterMl / 1000.0f * cycleDoseAMlPerL;
    if (p == IRRIG_DOSE_B) return cycleWaterMl / 1000.0f * cycleDoseBMlPerL;
    return 0;
}
static uint32_t currentDay() {
    time_t now = time(nullptr);
    return now >= 100000 ? (uint32_t)((now + 7 * 3600) / 86400) : 0;
}
static void loadBudget() {
    if (budgetLoaded) return;
    budgetLoaded = true;
    prefs.begin("smartgh_irig", false);
    usedMl = prefs.getFloat("used_ml", 0);
    usedDay = prefs.getUInt("used_day", 0);
    lastCycleEpoch = prefs.getUInt("last_cycle", 0);
    if (!isfinite(usedMl) || usedMl < 0) usedMl = 0;
}
static void persistBudget() {
    prefs.putFloat("used_ml", usedMl); prefs.putUInt("used_day", usedDay);
    prefs.putUInt("last_cycle", lastCycleEpoch);
}
static void rollBudget() {
    loadBudget();
    uint32_t day = currentDay();
    if (day && day > usedDay) { usedDay = day; usedMl = 0; persistBudget(); }
}
static float pFactorSaatIni() {
    time_t now = time(nullptr);
    int month = 1;
    if (now >= 100000) { struct tm local; localtime_r(&now, &local); month = local.tm_mon + 1; }
    return SUN_P_MONTH[(month >= 1 && month <= 12) ? month - 1 : 0];
}
static bool fertilizerConfigValid() {
    float a = phaseDoseAMlPerL(), b = phaseDoseBMlPerL();
    return FERTILIZER_DOSE_ENABLED && isfinite(a) && isfinite(b) && a > 0 && b > 0 &&
        a <= 100.0f && b <= 100.0f &&
        FILL_FLOW_ML_S > 0 && DOSE_A_FLOW_ML_S > 0 &&
        DOSE_B_FLOW_ML_S > 0 && DIST_FLOW_ML_S > 0;
}
static bool cycleFitsLimits(float waterMl, float doseAMlPerL, float doseBMlPerL) {
    const float aMl = waterMl / 1000.0f * doseAMlPerL;
    const float bMl = waterMl / 1000.0f * doseBMlPerL;
    const float finalMl = waterMl + aMl + bMl;
    const float volumes[] = {waterMl, aMl, bMl, finalMl};
    const float rates[] = {FILL_FLOW_ML_S, DOSE_A_FLOW_ML_S, DOSE_B_FLOW_ML_S, DIST_FLOW_ML_S};
    if (!(MIX_TANK_CAPACITY_ML > 0) || !isfinite(finalMl) || finalMl > MIX_TANK_CAPACITY_ML ||
        waterMl > IRRIG_MAX_CYCLE_ML || aMl <= 0 || bMl <= 0 ||
        aMl > IRRIG_MAX_DOSE_ML || bMl > IRRIG_MAX_DOSE_ML ||
        aMl > STOCK_A_CAPACITY_ML || bMl > STOCK_B_CAPACITY_ML) return false;
    for (uint8_t i = 0; i < 4; ++i) {
        float durationMs = durationSeconds(volumes[i], rates[i]) * 1000.0f;
        if (!(rates[i] > 0) || !isfinite(durationMs) || durationMs <= 0 ||
            durationMs > IRRIG_PHASE_TIMEOUT_MS) return false;
    }
    return true;
}

float calculateEtc() {
    float tmean = irrigationTmean();
    if (!isfinite(tmean)) return 0;
    float eto = constrain(pFactorSaatIni() * (BC_T_COEF * tmean + BC_T_CONST), 0.0f, MAX_ET0);
    return eto * phaseKc();
}
void irrigationFeedTemp(float temp) {
    if (!isfinite(temp)) return;
    if (!temperatureInit) {
        for (uint16_t i = 0; i < BC_TMEAN_BUCKETS; ++i) temperatures[i] = NAN;
        temperatureBucket = millis() / BC_TMEAN_BUCKET_MS;
        temperatureInit = true;
    }
    uint32_t bucket = millis() / BC_TMEAN_BUCKET_MS;
    if (bucket != temperatureBucket) {
        uint32_t gap = bucket - temperatureBucket;
        uint32_t steps = min(gap, (uint32_t)BC_TMEAN_BUCKETS);
        for (uint32_t k = 1; k <= steps; ++k) {
            uint32_t idx = (temperatureBucket + k) % BC_TMEAN_BUCKETS;
            if (isfinite(temperatures[idx])) { temperatureSum -= temperatures[idx]; --temperatureCount; }
            temperatures[idx] = NAN;
        }
        temperatureBucket = bucket;
    }
    uint32_t idx = bucket % BC_TMEAN_BUCKETS;
    if (isfinite(temperatures[idx])) temperatureSum -= temperatures[idx];
    else ++temperatureCount;
    temperatures[idx] = temp; temperatureSum += temp;
}
float irrigationTmean() {
    if (temperatureCount < BC_TMEAN_BUCKETS) return NAN;
    return temperatureSum / temperatureCount;
}
float irrigationDailyBudgetMl() {
    float etc = calculateEtc();
    return etc > 0 && IRRIG_EFFICIENCY > 0 && IRRIG_EFFICIENCY <= 1.0f
        ? etc * IRRIG_AREA_CM2 * 0.1f / IRRIG_EFFICIENCY : 0;
}
float irrigationDoseMl() {
    return IRRIG_CYCLES_PER_DAY > 0 ? min(irrigationDailyBudgetMl() / IRRIG_CYCLES_PER_DAY, IRRIG_MAX_CYCLE_ML) : 0;
}
unsigned long irrigationDoseMs() { return durationMs(irrigationDoseMl(), DIST_FLOW_ML_S); }
unsigned long irrigationMinIntervalMs() { return IRRIG_CYCLES_PER_DAY > 0 ? 86400000UL / IRRIG_CYCLES_PER_DAY : 0; }
IrrigStatus irrigationStatus() {
    IrrigStatus s{}; s.budgetMl = irrigationDailyBudgetMl(); s.usedMl = usedMl;
    s.phaseName = irrigationPhaseName();
    s.result = processResult;
    s.phaseTargetMl = phase == IRRIG_IDLE || phase == IRRIG_SETTLE ? 0 : phaseVolume(phase);
    s.phaseDurationMs = phase == IRRIG_IDLE ? 0 : phaseDurationMs;
    s.phaseEstimatedMl = phase == IRRIG_SETTLE || phase == IRRIG_IDLE ? 0 :
        flowRate(phase) * ((float)phaseDurationMs / 1000.0f);
    s.doseAMl = cycleWaterMl > 0 ? cycleWaterMl / 1000.0f * cycleDoseAMlPerL :
        irrigationDoseMl() / 1000.0f * phaseDoseAMlPerL();
    s.doseBMl = cycleWaterMl > 0 ? cycleWaterMl / 1000.0f * cycleDoseBMlPerL :
        irrigationDoseMl() / 1000.0f * phaseDoseBMlPerL();
    float plannedWaterMl = cycleWaterMl > 0 ? cycleWaterMl : irrigationDoseMl();
    float plannedA = cycleWaterMl > 0 ? cycleDoseAMlPerL : phaseDoseAMlPerL();
    float plannedB = cycleWaterMl > 0 ? cycleDoseBMlPerL : phaseDoseBMlPerL();
    s.fillDurationMs = durationMs(plannedWaterMl, FILL_FLOW_ML_S);
    s.doseADurationMs = durationMs(plannedWaterMl / 1000.0f * plannedA, DOSE_A_FLOW_ML_S);
    s.doseBDurationMs = durationMs(plannedWaterMl / 1000.0f * plannedB, DOSE_B_FLOW_ML_S);
    s.distDurationMs = durationMs(plannedWaterMl + plannedWaterMl / 1000.0f * (plannedA + plannedB), DIST_FLOW_ML_S);
    s.minIntervalMs = irrigationMinIntervalMs();
    s.doseMl = phase == IRRIG_IDLE ? irrigationDoseMl() : cycleWaterMl;
    s.doseMs = phase == IRRIG_IDLE ? irrigationDoseMs() : durationMs(cycleWaterMl, DIST_FLOW_ML_S);
    s.holdReason = holdReason; return s;
}
const char* irrigationPhaseName() {
    switch (phase) {
        case IRRIG_FILL: return "FILL"; case IRRIG_DOSE_A: return "DOSE_A";
        case IRRIG_DOSE_B: return "DOSE_B"; case IRRIG_SETTLE: return "SETTLE";
        case IRRIG_DIST: return "DIST"; default: return "IDLE";
    }
}
bool irrigationRunning() { return phase != IRRIG_IDLE; }
static void startPhase(IrrigPhase next, uint32_t now) {
    allPumpsOff(); phase = next; phaseStart = now;
    if (next == IRRIG_SETTLE) phaseDurationMs = IRRIG_SETTLE_MS;
    else {
        float rate = flowRate(next), volume = phaseVolume(next);
        if (!(rate > 0) || !isfinite(volume) || volume < 0 || volume > IRRIG_MAX_DOSE_ML && (next == IRRIG_DOSE_A || next == IRRIG_DOSE_B)) {
            strncpy(holdReason, "konfigurasi volume/debit tidak aman", sizeof(holdReason)-1);
            phase = IRRIG_IDLE; allPumpsOff(); return;
        }
        phaseDurationMs = durationMs(volume, rate);
        if (!phaseDurationMs || phaseDurationMs > IRRIG_PHASE_TIMEOUT_MS) {
            strncpy(holdReason, "durasi pompa melewati batas", sizeof(holdReason)-1);
            phase = IRRIG_IDLE; allPumpsOff(); return;
        }
        setPump(next == IRRIG_FILL ? PUMP_FILL : next == IRRIG_DOSE_A ? PUMP_DOSE_A : next == IRRIG_DOSE_B ? PUMP_DOSE_B : PUMP_DIST, true);
    }
    Serial.printf("[irigasi] fase %s, target %.2f mL durasi %.2f s\n", irrigationPhaseName(), phaseVolume(next), phaseDurationMs / 1000.0f);
}
void irrigationAbort() {
    if (phase != IRRIG_IDLE) {
        processResult = "FAILED";
        strncpy(holdReason, "proses dibatalkan oleh sensor/override", sizeof(holdReason)-1);
        holdReason[sizeof(holdReason)-1] = '\0';
    }
    allPumpsOff(); phase = IRRIG_IDLE;
}
void irrigationUpdate(bool trigger, bool manual, uint32_t requestId) {
    uint32_t now = millis(); rollBudget();
    if (phase == IRRIG_IDLE && trigger) {
        bool manualNew = manual && requestId != lastManualRequestId;
        if (manual) lastManualRequestId = requestId;
        float water = irrigationDoseMl();
        uint32_t day = currentDay();
        time_t epoch = time(nullptr);
        bool clockOk = epoch >= 100000;
        uint32_t since = clockOk && lastCycleEpoch ? (uint32_t)epoch - lastCycleEpoch : UINT32_MAX;
        bool intervalOk = !lastCycleEpoch || (clockOk && since >= irrigationMinIntervalMs() / 1000UL);
        bool budgetOk = usedMl + water <= irrigationDailyBudgetMl() + 0.5f;
        if (!IRRIG_AUTO_ENABLED && !manual) strncpy(holdReason, "irigasi otomatis dinonaktifkan", sizeof(holdReason)-1);
        else if (!fertilizerConfigValid()) strncpy(holdReason, "dosis AB Mix belum divalidasi/diaktifkan", sizeof(holdReason)-1);
        else if (!clockOk) strncpy(holdReason, "menunggu sinkronisasi waktu", sizeof(holdReason)-1);
        else if (water <= 0 || water > IRRIG_MAX_CYCLE_ML || irrigationTmean() != irrigationTmean()) strncpy(holdReason, "ETc/Tmean 24 jam belum tersedia", sizeof(holdReason)-1);
        else if (!manualNew && (!budgetOk || !intervalOk)) strncpy(holdReason, !budgetOk ? "anggaran harian habis" : "jeda minimum belum lewat", sizeof(holdReason)-1);
        else if (!intervalOk) strncpy(holdReason, "jeda minimum belum lewat sejak siklus terakhir", sizeof(holdReason)-1);
        else if (!cycleFitsLimits(water, phaseDoseAMlPerL(), phaseDoseBMlPerL())) strncpy(holdReason, "volume/durasi melebihi batas atau kapasitas tangki belum diatur", sizeof(holdReason)-1);
        else {
            holdReason[0] = 0; cycleWaterMl = water;
            cycleDoseAMlPerL = phaseDoseAMlPerL(); cycleDoseBMlPerL = phaseDoseBMlPerL();
            lastCycleEpoch = (uint32_t)epoch;
            usedMl += water; if (day) usedDay = day; persistBudget();
            processResult = "RUNNING";
            startPhase(IRRIG_FILL, now);
        }
    }
    if (phase == IRRIG_IDLE && holdReason[0]) processResult = "HELD";
    if (phase == IRRIG_IDLE && holdReason[0] && strcmp(holdReason, lastPrintedHold) != 0) {
        strncpy(lastPrintedHold, holdReason, sizeof(lastPrintedHold) - 1);
        lastPrintedHold[sizeof(lastPrintedHold) - 1] = '\0';
        Serial.printf("[irigasi] siklus ditahan: %s\n", holdReason);
    } else if (!holdReason[0]) lastPrintedHold[0] = '\0';
    if (phase == IRRIG_IDLE) return;
    if (now - phaseStart >= IRRIG_PHASE_TIMEOUT_MS && phase != IRRIG_SETTLE) { irrigationAbort(); strncpy(holdReason, "timeout pompa; siklus dibatalkan", sizeof(holdReason)-1); return; }
    if (now - phaseStart < phaseDurationMs) return;
    switch (phase) {
        case IRRIG_FILL: startPhase(IRRIG_DOSE_A, now); break;
        case IRRIG_DOSE_A: startPhase(IRRIG_DOSE_B, now); break;
        case IRRIG_DOSE_B: startPhase(IRRIG_SETTLE, now); break;
        case IRRIG_SETTLE: startPhase(IRRIG_DIST, now); break;
        case IRRIG_DIST: allPumpsOff(); phase = IRRIG_IDLE; processResult = "SUCCESS"; Serial.println("[irigasi] siklus selesai"); break;
        default: irrigationAbort(); break;
    }
}
