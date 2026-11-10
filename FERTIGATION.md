# Fertigation: configuration and commissioning

## Implemented cycle

`IDLE → FILL → DOSE_A → DOSE_B → SETTLE (30 s) → DIST → IDLE`.
Only one pump is energized at a time. All four outputs are initialized OFF and
the state machine aborts on invalid SHT31 data, a forced-off command, unsafe
configuration, or the per-phase timeout. There is no level sensor, mixer, TDS,
or flow-sensor dependency. Volume is estimated as calibrated `mL/s × seconds`.
Each pump phase is capped at 600 s, water per cycle at 18 L, each stock dose at
500 mL, and final mixed volume at configured tank capacity. Current configured
capacities: mixing tank 18,000 mL; stock A and B 6,216 mL each. These are
software limits only; without level sensors, actual fill levels are not sensed.

The tank capacities and timeout are configured, but nutrient dosing is
deliberately disabled: `FERTILIZER_DOSE_ENABLED=0` and all A/B rates are zero.
This prevents a cycle until the product formulation/dose is validated; the
provided Hydro J dose remains documented below as reference only.

## GPIO and pump rates

| Pump | Route | GPIO | Initial measured rate |
|---|---|---:|---:|
| FILL | Main reservoir → mixing tank | 27 | 125.36 L/h (34.822 mL/s) |
| Dosing A | A stock → mixing tank | 13 | 15.79 L/h (4.386 mL/s) |
| Dosing B | B stock → mixing tank | 14 | 15.79 L/h (4.386 mL/s) |
| DIST | Mixing tank → crop | 32 | 125.36 L/h (34.822 mL/s) |

Rates and per-pump correction multipliers are in `firmware/src/config.h`.
The rates are the latest user-reported measurements; no raw replicate volumes
or run times were available in the repository, so they are not independently
audited physical calibrations. Calibrate each installed pump separately:
collect output for a measured interval (preferably ≥30 s), repeat three times,
then set `correction = measured_average_mL / (configured_mL_per_s × seconds)`.
Use the same supply voltage, hose, lift, and restriction as the installation.
Never connect a pump directly to an ESP32 pin; use a correctly rated driver,
fuse, and common signal ground. Confirm GPIO27/13/14 against the actual wiring
before connecting drivers; these pins were free in the repository mapping only.

## Water estimate and feedback

The firmware uses `ETo = p × (0.46 × Tmean + 8)` and `ETc = Kc × ETo`.
`p` is the FAO-24 monthly table value interpolated for about 7° S (Semarang);
the method is a Blaney–Criddle approximation, not FAO-56 reference ETo.
Filtered SHT31 samples enter 1-minute buckets; irrigation is held until a full
24-hour window exists. Daily net depth is converted using `1 mm × 1 m² = 1 L`,
the configured 9,000 cm² area, application efficiency, and two daily cycles.
Soil-moisture/fuzzy demand remains the trigger; ETc supplies the volume budget.
Check the configured area against the wetted area actually served.
The `p` convention/table and the formula are described in FAO, *Irrigation
Water Management: Irrigation Water Needs*, Training Manual 3 (Brouwer &
Heibloem, 1986), Part II, Chapter 3, section 3.1.3, Table 4:
[FAO source](https://www.fao.org/4/s2022e/s2022e07.htm). That guide describes
Blaney–Criddle as a rough estimate and its equation's monthly-mean context;
this firmware uses a rolling 24-hour temperature mean as the available
operational approximation, not a monthly climate normal.

Firebase appends `fertigation_phase`, `fertigation_result` (`IDLE`, `RUNNING`,
`HELD`, `FAILED`, `SUCCESS`), current phase target/duration/estimated volume,
per-pump planned durations, calculated A/B dose targets, Kc and soil moisture to
existing sensor-log records.
Existing `etc` and legacy `et0` fields remain for dashboard compatibility;
`et0_mm_day` and `etc_mm_day` are explicit unit-bearing fields.

Kc is retained from the project proposal table, selected via the existing
Firebase `set_fase` command and persisted phase name:

| Project phase | Kc | Basis |
|---|---:|---|
| Awal | 0.40 | Existing proposal table (Supriadi et al. [10], citation as recorded in config) |
| Vegetatif | 0.75 | Same |
| Pembungaan | 1.10 | Same |
| Pembuahan / pemuliaan buah | 1.00 | Same |
| Pemasakan / panen | 0.90 | Same |

FAO-56 Chapter 6 reports green pepper Kc about 0.60 initial, 1.05 mid-season,
and 0.90 late-season; those are comparison values for a different staged
framework and do not replace the established project table. Reference: Allen,
Pereira, Raes & Smith (1998), FAO Irrigation and Drainage Paper 56,
[Chapter 6](https://www.fao.org/4/x0490e/x0490e0b.htm), [contents / citation](https://www.fao.org/4/x0490e/x0490e00.htm).

## AB Mix recipe and limits

The supplied Hydro J product page specifies equal A/B stock volumes by crop
stage. The values below are reference only and are currently disabled in
firmware. The product page URL is
[Hydro J Nutrisi Cabe 25 L Pekatan 1 kg](https://bibitonline.com/produk/hydro-j-nutrisi-cabe-25-l-pekatan-1-kg).
The page could not be fetched independently during this run; dose text below is
the user-provided label guidance and applies only to the matching product and
stock concentration.

| Phase selected in Firebase | Stock A | Stock B | Implemented mapping |
|---|---:|---:|---|
| Semai / Awal | 3 mL/L | 3 mL/L | disabled (`0`) |
| Vegetatif | 5 mL/L | 5 mL/L | disabled (`0`) |
| Generatif, Pembungaan, Buah, Panen | 7 mL/L | 7 mL/L | disabled (`0`) |

Unknown phase names fail the dosing validation instead of silently selecting a
recipe. Dose is calculated separately as `water_liters × phase mL/L`; A and B
run sequentially. The cited experimental studies below concern crop fertigation
and do not independently verify Hydro J's product-specific stock recipe:

| Evidence | What it supports | Why it does not set this product's mL/L |
|---|---|---|
| Gouthami, Devi, Kumar & Ramulu (2021), capsicum N/K fertigation, [DOI](https://doi.org/10.9734/ijecc/2021/v11i1230593) | Fertigation response varies with nutrient program | Nutrient levels are not a transferable AB stock recipe |
| Reddy & Hebbar (2017), red chilli fertigation and mulching, [DOI](https://doi.org/10.46492/ijai/2017.2.2.11) | Chilli fertigation has crop-specific experimental programs | Does not establish an unspecified commercial AB Mix ratio |
| Lob et al. (2023), chilli fertigation with silicon, [DOI](https://doi.org/10.55230/mabjournal.v52i2.2648) | Fertigation formulation matters | Silicon treatment is not a generic A/B dose guide |

Before the Expo, confirm the package identity and stock preparation against
the supplied product instructions, and test a small batch. This firmware does
not measure EC/pH or tank level. Never use these rates with a different AB Mix.

## Hardware test and calibration checklist

1. Disconnect pump power; build/flash standalone
   `firmware/tools/pump_calibration_test/pump_calibration_test.ino` and confirm
   boot outputs remain OFF.
2. Verify GPIO and active-HIGH/LOW driver polarity. At 115200 baud/Newline,
   use one command at a time (`FILL 30`, `A 30`, `B 30`, `DIST 30`); every test
   stops automatically, with `STOP` available for immediate shutdown.
3. Calibrate FILL and DIST independently into a graduated container; use small
   graduated syringes/cylinders for dosing pumps. Update the four
   correction factors and repeat at installed head/voltage.
4. Confirm Hydro J product/stock before changing the dosing gate/rates. Current
   dosing is disabled; the configured tank limits are 18,000 mL mixing and
   6,216 mL for each stock tank.
5. Confirm GPIO27 fills the mixing tank, 13/14 are the correct A/B lines, and
   GPIO32 distributes to the crop. Verify no outlets or tanks can overflow;
   this firmware has no level feedback.
6. Test with water before nutrient stock; verify FILL → A → B → 30 s all-off
   settle → DIST, then test SHT31 fault,
   forced-off, restart, and timeout behavior.

Flow-sensor test sketches under `firmware/tools/flow_sensor_test` remain
standalone bench tools and are not used by production control.
