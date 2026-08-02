# German band list for the spectrum analyzer

## Problem

`app/spectrum.h`'s `freqPresets[]` table (the band presets shown/auto-matched in
the spectrum analyzer's status line) is derived from fagci's original list, which
uses US-width ham bands (e.g. 2m 144–148 MHz, 6m 50–54 MHz) and is missing the
German 70cm amateur band (430–440 MHz) entirely — that range is only indirectly
covered by the license-free "LPD"/"PMR" entries, never labeled as an amateur
band. Several other entries (Railway, River1/River2, Satcom, GSM-UP/DN) turned
out on research to be either factually wrong for Germany or not practically
useful (see "Research findings" below).

The existing list should stay available (nothing here should be assumed
acceptable to rip out for other users of this fork), but a German-focused
alternative should be selectable.

## Research findings

Verified via web research (BNetzA, Wikipedia, CEPT/ICAO sources — see chat
history for full citations) against the current firmware values:

| Entry | Current value | Finding |
|---|---|---|
| Railway | 151.75–156.00 MHz | Wrong for any real service. Modern German rail radio is GSM-R (876–880 / 921–925 MHz, digital/encrypted — not meaningfully receivable as an FM scan target). Legacy analog Rangierfunk is a single frequency (166.470 MHz), not a band. Dropped. |
| Sea / River1 / River2 | 156.00–163.275 / 300.0125–300.5125 / 336.0125–336.5125 MHz | Sea's range was close but slightly wide; River1/River2 (~300 MHz) don't correspond to any real German inland-waterway allocation — maritime and inland-waterway radio share the *same* 156.000–162.025 MHz VHF band with different channel plans. Consolidated into one "Seefunk" entry. |
| CB | 26.975–28.000 MHz | Wrong on both edges (overlapped into 10m Ham). Correct German CB allocation is 26.565–27.405 MHz (80 channels: classic 1–40 at 26.965–27.405, extended 41–80 at 26.565–26.955). |
| PMR446 | 446.00625–446.20000 MHz | Correct (446.000–446.200 MHz). |
| LPD433 | 433.075–434.775 MHz | Correct, unchanged. Overlaps the German 70cm ham band — kept anyway per explicit decision (distinct legal category, useful for monitoring LPD devices specifically). |
| AirBand | 118.000–135.000 MHz | Slightly narrow; correct ICAO civil VHF aviation range is 117.975–137.000 MHz. |
| Satcom | 243.000–270.000 MHz | Technically plausible as military UHF SATCOM monitoring range, but mislabeled, mostly encrypted traffic, and receiving non-public traffic not addressed to you is a legal grey area under German telecom law (TKG §148) even RX-only. Dropped. |
| GSM-UP / GSM-DN | 89.0–91.5 / 93.5–96.0 MHz | Digital/encrypted, not meaningfully receivable — dropped (not a research finding, just not useful). |
| FRS 462 / FRS 467 | — | US service (Family Radio Service), not relevant in Germany. Dropped for the German list. |

The German amateur band edges (17m/15m/12m/10m/6m/2m/70cm/23cm) were checked
against a single ham-operator reference site, cross-checked against general
knowledge of IARU Region 1 / German allocations (which agree) — not against the
official BNetzA Frequenznutzungsplan/Amateurfunkverordnung directly. Judged
sufficient for this use case (spectrum analyzer preset labels, not a legal
reference document).

## Architecture

A new build flag, `ENABLE_BAND_LIST_GERMANY`, follows the existing `ENABLE_*`
convention (Makefile `-D` flag). In `app/spectrum.h`, the single `freqPresets[]`
definition is replaced by two definitions guarded by `#ifdef
ENABLE_BAND_LIST_GERMANY` / `#else` / `#endif`. Only one is compiled into any
given build — no flash cost for the unselected table, consistent with this
repo's tight flash budget (see `CLAUDE.md`'s flash-budget table).

The existing (fagci-derived) table's *content* is untouched. The only change
that affects both tables is `FreqPreset.name`, which grows from `char[8]` to
`char[12]` (7 usable chars → 11). This was confirmed safe for the display: the
preset name is drawn at `DrawStatus()` (`app/spectrum.c:785`,
`GUI_DisplaySmallest(matchedPreset->name, 0, 1, true, true)`), and the next
element sharing that row (the modulation-mode string) starts at x=116px — at
~4px/char (3×5 font + 1px spacing) that's headroom for ~29 characters, so 11 is
well within bounds. The cost is flash only: ~165 bytes total across both
tables' entries (~40 entries × 4 extra bytes/entry).

## Data: the German list

16 entries, in ascending frequency order (matching the existing table's
convention). All values in the project's 10 Hz units. Modulation/step-size/filter
settings for each copy the values of the closest matching existing entry of the
same band class (USB + narrower filter for HF ham bands, FM + wide filter for
VHF/UHF ham and PMR/LPD-style bands, AM + narrow filter for broadcast bands) —
these are hardware/regulatory-driven parameters already established correctly
in the existing table, not something this feature needs to re-derive.

| Name | Start (MHz) | End (MHz) |
|---|---|---|
| 17mHam | 18.068 | 18.168 |
| 15mHam | 21.000 | 21.450 |
| 12mHam | 24.890 | 24.990 |
| 10mHam | 28.000 | 29.700 |
| 6mHam | 50.000 | 52.000 |
| 2mHam | 144.000 | 146.000 |
| 70cmHam | 430.000 | 440.000 |
| 23cmHam | 1240.000 | 1300.000 |
| 15mBC | 18.900 | 19.020 |
| 13mBC | 21.450 | 21.850 |
| 11mBC | 25.670 | 26.100 |
| CB | 26.565 | 27.405 |
| PMR446 | 446.000 | 446.200 |
| LPD433 | 433.075 | 434.775 |
| Flugfunk | 117.975 | 137.000 |
| Seefunk | 156.000 | 162.025 |
| LoRaWAN | 864.000 | 869.000 |

Presets below 18 MHz remain omitted (BK4819 hardware minimum, same constraint
as the existing table).

## Testing

Extend `tools/host_tests/build.sh` (or add a sibling script) to build and run
`test_spectrum` twice: once with the existing CFLAGS (default table) and once
with `-DENABLE_BAND_LIST_GERMANY` added. This repo has already been bitten
once by CFLAGS drift between host tests and the real Makefile (see
`build.sh`'s own comment) — the same discipline applies here: whichever `-D`
flag the Makefile gains for this feature must be mirrored in the host test
build for both configurations, not just the default.

Test assertions per configuration: for a handful of representative
frequencies, `DrawStatus()`'s preset-matching loop resolves to the expected
preset name (e.g. a frequency in 430–440 MHz resolves to "70cmHam" only when
`ENABLE_BAND_LIST_GERMANY` is defined). No new production logic is introduced
(the matching loop in `app/spectrum.c:777-783` is unchanged), so testing is
about confirming the right table's data made it into the binary, not new
behavior.

## Out of scope

- Runtime switching between band lists (explicitly decided against — compile-time only).
- Re-verifying the amateur band edges against the official BNetzA
  Frequenznutzungsplan directly (judged unnecessary for this use case).
- Any change to the existing (default) table's content.
