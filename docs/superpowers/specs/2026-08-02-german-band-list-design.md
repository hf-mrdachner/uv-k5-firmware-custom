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

Two independent build flags, following the existing `ENABLE_*` convention
(Makefile `-D` flags), both defaulting off:

- **`ENABLE_DE_HAM_BANDS`** — corrects the 8 amateur-radio entries to the
  German allocations (adds 17m and 70cm, narrows 6m/2m to the German width).
  These are the bands a licensed operator may actually *transmit* on with
  this radio.
- **`ENABLE_DE_EXTRA_BANDS`** — corrects/replaces the remaining 9 non-amateur
  entries (CB, PMR446, LPD433, Flugfunk, Seefunk, LoRaWAN — plus dropping
  Railway/Satcom/River1/River2/FRS/GSM). These are receive-only reference
  bands: even where the band itself is license-free (PMR446, CB), the UV-K5
  is not a certified device for it, so transmitting on it with this radio
  isn't legal regardless of the flag — the flag only affects what's shown/
  auto-matched when listening.

The two flags are independent (either can be on without the other). With
both off, the table is byte-for-byte identical to today's — nothing existing
is removed or changed unless explicitly opted into.

This rules out the simpler "two full array literals" approach (only 2 of the
4 combinations would be representable that way). Instead, `app/spectrum.h`
keeps a **single** `freqPresets[]` definition, where each entry that differs
between flag states is wrapped in its own `#if defined(ENABLE_DE_HAM_BANDS)`
/ `#else` / `#endif` (or `ENABLE_DE_EXTRA_BANDS`) block choosing that one
entry's name/values; entries neither flag touches (15mBC, 13mBC, 11mBC,
15mHam, 12mHam, 10mHam, LoRaWAN, 23cmHam — all identical between the German
and international values) stay unconditional. See "Data" below for the exact
per-entry rule.

**Known ordering exception:** LPD433 (433.075–434.775 MHz) sits entirely
inside the German 70cm ham band (430–440 MHz) — kept as a deliberate,
explicit decision (see chat history) rather than merged away. Because
`AutomaticPresetChoose()`/`DrawStatus()` match on "first array entry whose
range contains the frequency" (`app/spectrum.c:777-783`, `1449-1457`), the
LPD/LPD433 entry must be placed *before* the 70cmHam entry in source order
whenever both can be compiled in (i.e. whenever `ENABLE_DE_HAM_BANDS` is on),
so a frequency inside LPD433's slice is labeled "LPD433"/"LPD", not
"70cmHam". This is the only place in the table where source order isn't
purely ascending by frequency — `SelectNearestPreset()`'s KEY_3/KEY_9
next/prev navigation (`app/spectrum.c:1420-1444`) will, right at that one
boundary, jump to LPD's start (433.075 MHz) before reaching 70cmHam's actual
start (430.000 MHz) coming from below. Accepted as a minor, understood
trade-off for getting the passive status-line match right; must be covered
by a host test (see "Testing").

`FreqPreset.name` grows from `char[8]` to `char[12]` (7 usable chars → 11,
independent of either flag — needed either way for names like "Flugfunk").
Confirmed safe for the display: the preset name is drawn at `DrawStatus()`
(`app/spectrum.c:785`, `GUI_DisplaySmallest(matchedPreset->name, 0, 1, true,
true)`), and the next element sharing that row (the modulation-mode string)
starts at x=116px — at ~4px/char (3×5 font + 1px spacing) that's headroom for
~29 characters, so 11 is well within bounds. The cost is flash only: ~165
bytes total across the table's ~24 entries (4 extra bytes/entry).

## Data: per-entry rule

All values in the project's 10 Hz units (`Hz / 10`). "Always" = unconditional,
identical regardless of either flag.

**Touched by `ENABLE_DE_HAM_BANDS`** (8 entries — int'l value used when off):

| Name | German (flag on) | International (flag off) |
|---|---|---|
| 17mHam | 1806800–1816800 | *(absent)* |
| 6mHam | 5000000–5200000 | 5000000–5400000 |
| 2mHam | 14400000–14600000 | 14400000–14800000 |
| 70cmHam | 43000000–44000000 | *(absent)* |

(15mHam, 12mHam, 10mHam, 23cmHam are identical in both — always present,
unconditional, unchanged from today's values.)

**Touched by `ENABLE_DE_EXTRA_BANDS`** (name and/or values differ, or entry is
dropped, when on):

| Slot | German (flag on) | International (flag off) |
|---|---|---|
| CB | "CB" 2656500–2740500 | "CB" 2697500–2799990 |
| AirBand/Flugfunk | "Flugfunk" 11797500–13700000 | "AirBand" 11800000–13500000 |
| Railway | *(absent)* | "Railway" 15175000–15599990 |
| Sea/Seefunk | "Seefunk" 15600000–16202500 | "Sea" 15600000–16327500 |
| Satcom | *(absent)* | "Satcom" 24300000–27000000 |
| River1 | *(absent)* | "River1" 30001250–30051250 |
| River2 | *(absent)* | "River2" 33601250–33651250 |
| LPD/LPD433 | "LPD433" 43307500–43477500 | "LPD" 43307500–43477500 |
| PMR/PMR446 | "PMR446" 44600000–44620000 | "PMR" 44600625–44620000 |
| FRS 462 | *(absent)* | "FRS 462" 46256250–46272500 |
| FRS 467 | *(absent)* | "FRS 467" 46756250–46771250 |
| GSM-UP | *(absent)* | "GSM-UP" 89000000–91500000 |
| GSM-DN | *(absent)* | "GSM-DN" 93500000–96000000 |

(15mBC, 13mBC, 11mBC, LoRaWAN are identical in both — always present,
unconditional, unchanged.)

Modulation/step-size/filter settings for every entry copy the values of
today's closest matching entry of the same band class (USB + narrower filter
for HF ham bands, FM + wide filter for VHF/UHF ham and PMR/LPD-style bands,
AM + narrow filter for broadcast bands) — hardware/regulatory-driven
parameters already established correctly, not something this feature
re-derives. New entries (17mHam, 70cmHam) copy the settings of the existing
entry closest in both frequency and character (17mHam ← 15mHam/12mHam/10mHam's
USB/narrower-filter convention; 70cmHam ← 2mHam's FM/wide-filter convention).

Presets below 18 MHz remain omitted (BK4819 hardware minimum, unchanged
constraint).

## Testing

Extend `tools/host_tests/build.sh` (or add sibling scripts/CFLAGS) to build
and run `test_spectrum` for at least 3 non-default configurations, in addition
to today's default (both flags off, must remain byte-for-byte passing
unchanged): `ENABLE_DE_HAM_BANDS` alone, `ENABLE_DE_EXTRA_BANDS` alone, and
both together. This repo has already been bitten once by CFLAGS drift between
host tests and the real Makefile (see `build.sh`'s own comment) — the same
discipline applies here: whichever `-D` flags the Makefile gains for this
feature must be mirrored in the host test build for every configuration
tested, not just the default.

Assertions needed, beyond "does the expected preset name match at a given
frequency":
- 70cmHam only resolves when `ENABLE_DE_HAM_BANDS` is on.
- A frequency inside LPD433's slice (e.g. 433.2 MHz) resolves to "LPD"/
  "LPD433", never "70cmHam", whenever `ENABLE_DE_HAM_BANDS` is on (the
  ordering exception above) — this is the one assertion that would silently
  break if the array's source order were ever "cleaned up" to be purely
  ascending.
- A frequency in 70cmHam but outside LPD433's slice (e.g. 435.5 MHz)
  resolves to "70cmHam" when `ENABLE_DE_HAM_BANDS` is on.
- Railway/Satcom/River1/River2/FRS/GSM frequencies resolve to no match at all
  when `ENABLE_DE_EXTRA_BANDS` is on (they're absent, not just renamed).

## Out of scope

- Runtime switching between band lists (explicitly decided against — compile-time only).
- Re-verifying the amateur band edges against the official BNetzA
  Frequenznutzungsplan directly (judged unnecessary for this use case).
- Any change to the existing (default, both-flags-off) table's content.
