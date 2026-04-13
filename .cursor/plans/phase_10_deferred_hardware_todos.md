# Phase 10 / 10b — Deferred hardware validation

**Created:** 2026-04-14  
**Context:** Phase 10 and 10b **firmware closure** was approved while the **load cell was not connected**. Nonsensical `F=` on UART without a bridge is expected. This file tracks what remains before **force accuracy** is production-qualified and before treating **P10-M5 / M6** (and related checks) as complete.

**Reference:** Objective metrics table in [.cursor/plans/snazzy-petting-mountain.md](snazzy-petting-mountain.md) (Phase 10 — “Objective exit metrics”).

---

## Blocker

- [ ] **Connect load cell** to the conditioned bridge inputs (excitation + sense per schematic). Verify wiring, shielding, and supply within ADS131M02 common-mode / gain limits.

---

## After load cell is connected (bench)

- [ ] **P10-M5 — Tare residual:** Unloaded, stable, press **`t`** / **`T`** (CDC). Confirm `DECIM:` / panel `F` meets **abs(force) ≤ 2.0 N** (tighten toward 0.5 N when quiet).
- [ ] **P10-M6 — Known load:** Apply certified mass (e.g. ~9.81 N). Confirm reading within **±0.5 N** of expected (or team-agreed tolerance).
- [ ] **P10-M4 (full window):** Confirm **zero additional DRDY misses** over **60 s** (sum per-second `miss=` deltas or log cumulative `missCount`).
- [ ] **P10-M7 — CSV framing (optional):** Spot-check `g_dpStagedCsv` in debugger or temporary UART dump vs regex in master plan.
- [ ] **P10-M9 (optional):** Second `.cal` / cell selection; confirm M1–M6 still pass and scaling matches UART cal fields.

---

## Phase 10b environmental checks (if not done yet)

- [ ] **Windows:** After card format, only **LOGGER** gets a drive letter; **SYSCAL** hidden from Explorer as per plan (Disk Management shows Linux-type second partition).
- [ ] **Reboot:** Cell selection not persisted — confirm expected behavior on power cycle.

---

## Notes

- **P10-M1–M4** (rates, 16:1 coupling, per-second `miss=0`) and **M8** (`.cal` load) can be validated **without** a load cell; they were the basis for firmware exit approval.
- Phase **11** (ring buffer, SD session files) does not depend on load cell wiring but benefits from sane **F** for end-to-end logging tests.
