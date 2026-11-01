<script setup>
import { computed } from 'vue';
import { state, actuators, actuatorModes, overrideSeconds, onSwitch, releaseAll } from '../store.js';

const a = computed(() => actuators.value);
// Mode yang DILAPORKAN perangkat: 'auto' | 'on' | 'off' | 'unknown'.
const modes = computed(() => actuatorModes.value);
// Sisa waktu override (detik), dihitung mundur dari jam lokal.
const left = computed(() => overrideSeconds.value);

// Telemetri irigasi: hanya durasi pompa dan jeda minimum. Dosis harian,
// anggaran, dan air terpakai sudah tampil di kartu metrik — jangan diulang.
// Field tidak ada (firmware lama) -> bloknya disembunyikan, bukan angka kosong.
const ig = computed(() => state.lastData?.irigasiInfo || null);
const igLine = computed(() => {
  const r = ig.value;
  if (!r) return null;
  const parts = [];
  if (r.doseMs) parts.push(Math.round(r.doseMs / 1000) + ' s pompa');
  if (r.intervalMin) parts.push('jeda min ' + Math.round(r.intervalMin / 60) + ' jam');
  return parts.length ? parts.join(' \u00b7 ') : null;
});

// Tiga posisi mode. Bukan checkbox: checkbox hanya bisa dua keadaan, padahal
// AUTO (ikut fuzzy) berbeda dari OFF (paksa mati).
const MODES = [
  { v: 'on', label: 'ON', cls: 'bg-ok text-card', tip: 'Paksa nyala' },
  { v: 'auto', label: 'AUTO', cls: 'bg-accent text-white', tip: 'Ikut fuzzy' },
  { v: 'off', label: 'OFF', cls: 'bg-danger text-card', tip: 'Paksa mati' },
];

// Kartu Kontrol tetap terlihat untuk semua orang (=status aktuator berguna untuk
// dipantau), tetapi segmented + "Kembali ke Auto" hanya untuk admin.
//
// `icon` = array path. Blower butuh ini: satu bilah digambar 3× lalu dirotasi
// 120°. Path lama mengulang path yang SAMA tanpa rotasi, jadi yang tampil
// gumpalan, bukan kipas. Bilahnya "sapu melingkar" (swept) — kelopok simetris
// justru terbaca sebagai bunga, bukan kipas.
const BLADE = 'M12 12c-.4-2.6.4-4.8 2.6-5.9 1.9-.9 4-.2 4.8 1.6.8 1.9-.1 4-1.9 4.9-1.6.8-3.2.3-4.4-.6';
const spin = d => [0, 120, 240].map(r => ({ d, t: `rotate(${r} 12 12)` }));

const items = computed(() => [
  { key: 'mist', label: 'Misting', on: a.value.mist, icon: [{ d: 'M4 15a7 7 0 1 1 11.7-7H17a4.5 4.5 0 0 1 2.5 8.2M8 16v1.5M12 16v2M16 16v1.5' }] },
  { key: 'irigasi', label: 'Irigasi', on: a.value.irigasi, icon: [{ d: 'M7 3s3 5 3 7a3 3 0 0 1-6 0c0-2 3-7 3-7zM17 10s3 5 3 7a3 3 0 0 1-6 0c0-2 3-7 3-7z' }],
    note: 'Paksa nyala menjalankan SATU siklus pompa; paksa mati membatalkan siklus.' },
  { key: 'blower', label: 'Blower', on: a.value.blower, icon: spin(BLADE), duty: a.value.level },
]);

// mm:ss untuk chip hitung mundur.
const mmss = s => Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');

// Chip hanya untuk mode paksa + sisa waktu diketahui. Mode AUTO tidak punya
// hitung mundur, dan firmware lama tanpa *_ov_sisa_s -> 0 (chip disembunyikan,
// bukan "00:00").
const chipOf = k => {
  const m = modes.value[k];
  const s = left.value[k];
  return (m === 'on' || m === 'off') && s > 0 ? { mode: m, text: mmss(s) } : null;
};
</script>

<template>
  <div class="rounded-xl border border-border bg-card p-5 max-[420px]:p-4">
    <div class="mb-4 flex flex-wrap items-center justify-between gap-3">
      <h2 class="flex items-center gap-[7px] text-[15px] font-semibold">
        <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>
        Kontrol Aktuator
      </h2>
      <span class="text-xs text-muted">{{ state.isAdmin ? 'Kontrol remot via Firebase RTDB' : 'Hanya lihat — login untuk kontrol manual' }}</span>
    </div>

    <!-- IRRIGOFF: irigasi otomatis sengaja dimatikan di perangkat (debit pompa
         belum dikalibrasi). Ini kondisi yang disengaja, bukan gangguan —
         tampil juga untuk tamu karena menyangkut kondisi perangkat. -->
    <div
      v-if="state.lastData && state.lastData.flags && state.lastData.flags.irrigOff"
      class="mb-3 rounded-lg border border-warn bg-warn-soft px-3 py-2 text-[11.5px] leading-snug text-warn"
    >
      <b>Irigasi otomatis dimatikan di perangkat</b> — debit pompa belum dikalibrasi. Perintah manual tetap bisa dijalankan.
    </div>

    <!-- Irigasi alasan tahan: peringatan (kuning), bukan info kecil.
         Kondisi perangkat -> tampil juga untuk tamu. -->
    <div
      v-if="ig && ig.holdReason"
      class="mb-3 rounded-lg border border-warn bg-warn-soft px-3 py-2 text-[11.5px] leading-snug text-warn"
    >
      <b>Siklus irigasi ditahan</b> — {{ ig.holdReason }}
    </div>

    <!-- Telemetri irigasi: durasi pompa dan jeda minimum. Disembunyikan kalau
         perangkat belum mengirimnya. -->
    <div
      v-if="igLine"
      class="mb-3 rounded-lg border border-border bg-card-2 px-3 py-2 text-[11px] leading-snug text-muted"
    >
      {{ igLine }}
    </div>

    <div class="flex flex-col gap-2.5">
      <div
        v-for="it in items" :key="it.key"
        class="relative rounded-lg border border-border bg-card-2 px-3.5 py-2.5"
      >
        <!-- Chip hitung mundur override, mengambang di tepi atas kartu. Muncul
             hanya saat mode paksa DAN firmware mengirim sisa waktunya. -->
        <span
          v-if="chipOf(it.key)"
          class="absolute -top-2 right-2.5 inline-flex items-center gap-1 rounded-full border border-warn-line bg-warn-soft px-2 py-[1px] text-[10px] font-bold tracking-wide text-warn"
          :title="`Override ${chipOf(it.key).mode === 'on' ? 'paksa nyala' : 'paksa mati'} — sisa ${chipOf(it.key).text}`"
        >
          <svg class="max-[420px]:hidden" viewBox="0 0 24 24" width="10" height="10" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg>
          OVERRIDE {{ chipOf(it.key).text }}
        </span>

        <div class="flex items-center gap-2.5">
          <svg class="shrink-0 text-accent" viewBox="0 0 24 24" width="17" height="17" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path v-for="(p, pi) in it.icon" :key="pi" :d="p.d" :transform="p.t" /></svg>

          <div class="min-w-0">
            <h3 class="whitespace-nowrap text-[13.5px] font-semibold">{{ it.label }}</h3>
            <!-- Keluaran fuzzy. Disembunyikan (bukan "OFF") kalau firmware
                 belum mengirim fuzzy_*: record lama tetap benar. -->
            <span v-if="a.fuzzy[it.key] !== null" class="block text-[10.5px] text-muted">
              FUZZY: {{ a.fuzzy[it.key] ? 'ON' : 'OFF' }}
            </span>
          </div>

          <div class="ml-auto flex items-baseline gap-2.5">
            <!-- Duty blower = keluaran fuzzy (PWM). -->
            <span v-if="it.duty !== undefined && it.duty !== null" class="text-[11.5px] text-muted">Duty {{ Math.round(it.duty * 100) }} %</span>
            <!-- Status besar = laporan perangkat (yang SEDANG berjalan), bukan
                 mode. Irigasi bisa OFF di sini sementara mode masih ON: siklus
                 sudah selesai, override belum dilepas. Itu benar. -->
            <span class="text-[15px] font-bold" :class="it.on ? 'text-ok' : 'text-danger'">{{ it.on ? 'ON' : 'OFF' }}</span>
          </div>
        </div>

        <!-- Segmented tiga posisi, admin saja. Penolakan sesungguhnya tetap di
             RTDB rules + sendCommand(). aria-pressed supaya mode terbaca
             pembaca layar (warna saja tidak cukup). -->
        <div
          v-if="state.isAdmin"
          class="mt-2 grid grid-cols-3 gap-1 rounded-lg border border-border bg-card p-1"
          role="group"
          :aria-label="`Mode ${it.label}`"
        >
          <button
            v-for="m in MODES" :key="m.v" type="button"
            class="rounded-md px-2 py-[3px] text-[11px] font-bold transition-colors"
            :class="modes[it.key] === m.v ? m.cls : 'text-muted hover:bg-card-2'"
            :aria-pressed="modes[it.key] === m.v"
            :title="`${it.label}: ${m.tip}`"
            @click="onSwitch(it.key, m.v)"
          >{{ m.label }}</button>
        </div>

        <p v-if="it.note" class="mt-1.5 text-[10.5px] leading-snug text-muted">{{ it.note }}</p>
      </div>
    </div>

    <p v-if="!state.isAdmin" class="mt-3 text-center text-[11px] text-muted">Status ON/OFF = laporan perangkat.</p>

    <!-- "Kembali ke Auto" = aksi semua-sekaligus; gayanya sekunder karena
         segmented per aktuator sudah cukup untuk kerja sehari-hari. -->
    <button
      v-if="state.isAdmin"
      type="button"
      class="mt-3 inline-flex w-full items-center justify-center gap-1.5 rounded-lg border border-border bg-card px-3 py-2 text-[12.5px] font-semibold text-muted hover:bg-card-2 hover:text-ink disabled:opacity-50"
      title="Lepas override manual semua aktuator — semua kembali ke fuzzy"
      @click="releaseAll()"
    >
      <svg class="text-accent" viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 12a9 9 0 1 1-3-6.7"/><path d="M21 3v6h-6"/></svg>
      Kembali ke Auto
    </button>

    <p v-if="state.cmdStatus" class="mt-3 min-h-[14px] text-center text-[11px] font-semibold text-muted">{{ state.cmdStatus }}</p>
    <p v-if="state.cmdError" class="mt-1 min-h-[14px] text-center text-[11px] font-semibold text-danger">{{ state.cmdError }}</p>
  </div>
</template>
