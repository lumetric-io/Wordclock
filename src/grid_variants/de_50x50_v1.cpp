#include "grid_variants/de_50x50_v1.h"

// German 11x11 layout for the 50x50 product.
//
// Word positions and letter grid are both taken from the real plate; the two
// were cross-checked against each other (see the letter-grid comment below).
//
// --------------------------------------------------------------------------
// LED index mapping (identical hardware to NL_50x50_V3 — same panel, same
// wiring, only the letter plate differs)
// --------------------------------------------------------------------------
// The strip runs boustrophedon over 13 LEDs per row: 11 letter cells plus one
// LED at each row end that sits behind the frame. Row 0 runs left→right.
//
//     index(r, c) = 13*r + 1  + c    for even r   (left → right)
//     index(r, c) = 13*r + 11 - c    for odd  r   (right → left)
//
// with r = 0..9 (letter rows, top to bottom) and c = 0..10 (columns, left to
// right as seen from the front). Derived from nl_50x50_v3.cpp and verified
// against all 21 of its words, including the vertical ones (HALF at column 8,
// TWEE at column 1, VIER at column 9, ELF at column 7).
//
// First/last letter index per row:
//   r0:   1..11    r1:  24..14    r2:  27..37    r3:  50..40    r4:  53..63
//   r5:  76..66    r6:  79..89    r7: 102..92    r8: 105..115   r9: 128..118
//
// Note that cell (9,0) = LED 128, i.e. the bottom-left letter sits at the same
// index where LED_COUNT_GRID says the "extra" region begins. That split is
// bookkeeping, not a hardware boundary: getActiveLedCountGrid() is not used
// anywhere outside grid_layout.cpp, and the AfterGrid layout addresses the
// strip via ledCountTotal (141). The minute dots are a full 13-LED row of
// their own starting at 130 — that is what makes 133/135/137/139 line up with
// the '-' positions in the dot row string. The Dutch plate simply parks an
// unused 'X' at (9,0); the German plate uses it for the S of SECHS.
// --------------------------------------------------------------------------

// Identical to NL_50x50_V3 — same panel. The multi-language design requires
// every variant within one product to share these counts.
const uint16_t LED_COUNT_GRID_DE_50x50_V1 = 128;
const uint16_t LED_COUNT_EXTRA_DE_50x50_V1 = 13;
const uint16_t LED_COUNT_TOTAL_DE_50x50_V1 =
    LED_COUNT_GRID_DE_50x50_V1 + LED_COUNT_EXTRA_DE_50x50_V1;

// --------------------------------------------------------------------------
// Letter grid — transcribed from the physical plate
// --------------------------------------------------------------------------
// Every word below was verified against these letters: all 24 spell their
// intended German word at their LED indices. The letters that carry no word
// are filler; note that r4c5 and r9c2 are literally the letter X, not a
// placeholder.
//
// The grid is used only for the setup preview (rendered so the user can match
// it against the plate on the wall) — the firmware addresses LEDs by index and
// never reads a letter.
//
// ⚠ ENCODING — the Ü in FÜNF and the Ö in ZWÖLF are two UTF-8 bytes each, so
// those rows are 12 bytes long while the grid is 11 cells wide. The preview
// endpoint must decode per code point, not per byte.
const char* const LETTER_GRID_DE_50x50_V1[] = {
  "ESQISTMZEHN",  // r0: ES(0-1) IST(3-5) ZEHN_M(7-10)
  "FÜNFZWANZIG",  // r1: FÜNF_M(0-3) ZWANZIG_M(4-10)
  "DREIVIERTEL",  // r2: DREIVIERTEL(0-10) VIERTEL(4-10) V=VOR head
  "PJMWOGNACHK",  // r3: VOR(c4, vertical) NACH(6-9)
  "KMGPRXJHALB",  // r4: VOR(c4, vertical) HALB(7-10)
  "SIEBENZWÖLF",  // r5: SIEBEN(0-5) ZWÖLF(6-10) + heads of SECHS/EINS/NEUN/ZWEI
  "EFISVEWACHT",  // r6: ACHT(7-10) + verticals SECHS/FÜNF/EINS/VIER/NEUN/ZWEI
  "CÜNJIUELFPG",  // r7: ELF(6-8) + verticals
  "HNSKENIZEHN",  // r8: ZEHN(7-10) + verticals
  "SFXDREIWUHR",  // r9: SECHS tail(0) FÜNF tail(1) DREI(3-6) UHR(8-10)
  "..-.-.-.-.."   // minute dots — own LED row starting at 130
};

// Same four dot LEDs as the Dutch 50x50 variant — language-independent.
const uint16_t EXTRA_MINUTES_DE_50x50_V1[] = {
  static_cast<uint16_t>(LED_COUNT_GRID_DE_50x50_V1 + 5),
  static_cast<uint16_t>(LED_COUNT_GRID_DE_50x50_V1 + 7),
  static_cast<uint16_t>(LED_COUNT_GRID_DE_50x50_V1 + 9),
  static_cast<uint16_t>(LED_COUNT_GRID_DE_50x50_V1 + 11)
};

// --------------------------------------------------------------------------
// Words — language-neutral slot keys (see §4.2 of multi-language-design.md)
// --------------------------------------------------------------------------
// (r,c) comments regenerated from the indices. Words marked "vertical" run
// down a column, like HALF/TWEE/VIER/ELF on the Dutch plate.
//
// Deliberate letter sharing between mutually exclusive words:
//   SIEBEN + SECHS share the S at (5,0)      ZWÖLF + ZWEI share the Z at (5,6)
//   ELF    + ZWEI  share the E at (7,6)      DREI  + VIER share the R at (9,4)
//   VIERTEL + VOR  share the V at (2,4)      DREIVIERTEL contains VIERTEL
const WordPosition WORDS_DE_50x50_V1[] = {
  // prefix — split in two so "ES"/"IST" can animate separately, like HET/IS
  WPOS("PREFIX_A",     1, 2),                             // ES      (0,0)-(0,1)
  WPOS("PREFIX_B",     4, 5, 6),                          // IST     (0,3)-(0,5)

  // minute words
  WPOS("MIN_5",        24, 23, 22, 21),                   // FÜNF    (1,0)-(1,3)
  WPOS("MIN_10",       8, 9, 10, 11),                     // ZEHN    (0,7)-(0,10)
  WPOS("MIN_20",       20, 19, 18, 17, 16, 15, 14),       // ZWANZIG (1,4)-(1,10)
  WPOS("QUARTER",      31, 32, 33, 34, 35, 36, 37),       // VIERTEL (2,4)-(2,10)
  WPOS("THREEQUARTER", 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37),
                                                          // DREIVIERTEL (2,0)-(2,10)
  WPOS("HALF",         60, 61, 62, 63),                   // HALB    (4,7)-(4,10)

  // direction
  WPOS("TO",           31, 46, 57),                       // VOR     col 4, r2-r4 (vertical)
  WPOS("PAST",         44, 43, 42, 41),                   // NACH    (3,6)-(3,9)

  // hours
  WPOS("H_1",          74, 81, 100, 107),                 // EINS    col 2, r5-r8 (vertical)
  WPOS("H_1_ALT",      74, 81, 100),                      // EIN     col 2, r5-r7 — "ein Uhr"
  WPOS("H_2",          70, 85, 96, 111),                  // ZWEI    col 6, r5-r8 (vertical)
  WPOS("H_3",          125, 124, 123, 122),               // DREI    (9,3)-(9,6)
  WPOS("H_4",          83, 98, 109, 124),                 // VIER    col 4, r6-r9 (vertical)
  WPOS("H_5",          80, 101, 106, 127),                // FÜNF    col 1, r6-r9 (vertical)
  WPOS("H_6",          76, 79, 102, 105, 128),            // SECHS   col 0, r5-r9 (vertical)
  WPOS("H_7",          76, 75, 74, 73, 72, 71),           // SIEBEN  (5,0)-(5,5)
  WPOS("H_8",          86, 87, 88, 89),                   // ACHT    (6,7)-(6,10)
  WPOS("H_9",          71, 84, 97, 110),                  // NEUN    col 5, r5-r8 (vertical)
  WPOS("H_10",         112, 113, 114, 115),               // ZEHN    (8,7)-(8,10)
  WPOS("H_11",         96, 95, 94),                       // ELF     (7,6)-(7,8)
  WPOS("H_12",         70, 69, 68, 67, 66),               // ZWÖLF   (5,6)-(5,10)

  WPOS("OCLOCK",       120, 119, 118),                    // UHR     (9,8)-(9,10)
};

const size_t WORDS_DE_50x50_V1_COUNT =
    sizeof(WORDS_DE_50x50_V1) / sizeof(WORDS_DE_50x50_V1[0]);
const size_t EXTRA_MINUTES_DE_50x50_V1_COUNT =
    sizeof(EXTRA_MINUTES_DE_50x50_V1) / sizeof(EXTRA_MINUTES_DE_50x50_V1[0]);

// ==========================================================================
// Phrase rules — two dialects on one plate
// ==========================================================================
// The tables differ on four steps only (:15, :20, :40, :45). This plate
// carries ZWANZIG, VIERTEL *and* DREIVIERTEL, so both are supported and the
// dialect stays a firmware setting instead of a second plate.
//
// EIN vs EINS: German says "es ist ein Uhr" but "fünf nach eins". The engine
// needs no extra data for that — on a step that lights OCLOCK it prefers
// H_<n>_ALT when the variant defines it (here: EIN), otherwise H_<n>. Dutch
// variants define no _ALT keys, so their behaviour is unchanged.

// Nord / "Hochdeutsch": zwanzig nach, viertel nach, zwanzig vor, viertel vor.
const PhraseRules DE_RULES_STANDARD = {
  "de",
  {
    /* :00 */ { { nullptr,   nullptr, nullptr }, 0, true  },  // es ist ein Uhr
    /* :05 */ { { "MIN_5",   "PAST",  nullptr }, 0, false },  // fünf nach
    /* :10 */ { { "MIN_10",  "PAST",  nullptr }, 0, false },  // zehn nach
    /* :15 */ { { "QUARTER", "PAST",  nullptr }, 0, false },  // viertel nach
    /* :20 */ { { "MIN_20",  "PAST",  nullptr }, 0, false },  // zwanzig nach
    /* :25 */ { { "MIN_5",   "TO",    "HALF"  }, 1, false },  // fünf vor halb
    /* :30 */ { { "HALF",    nullptr, nullptr }, 1, false },  // halb
    /* :35 */ { { "MIN_5",   "PAST",  "HALF"  }, 1, false },  // fünf nach halb
    /* :40 */ { { "MIN_20",  "TO",    nullptr }, 1, false },  // zwanzig vor
    /* :45 */ { { "QUARTER", "TO",    nullptr }, 1, false },  // viertel vor
    /* :50 */ { { "MIN_10",  "TO",    nullptr }, 1, false },  // zehn vor
    /* :55 */ { { "MIN_5",   "TO",    nullptr }, 1, false },  // fünf vor
  }
};

// Süd-Ost (Sachsen, Schwaben, Österreich): viertel/dreiviertel name the hour
// being worked towards, and :20/:40 go via halb.
const PhraseRules DE_RULES_SUED = {
  "de-sued",
  {
    /* :00 */ { { nullptr,        nullptr, nullptr }, 0, true  },  // es ist ein Uhr
    /* :05 */ { { "MIN_5",        "PAST",  nullptr }, 0, false },  // fünf nach
    /* :10 */ { { "MIN_10",       "PAST",  nullptr }, 0, false },  // zehn nach
    /* :15 */ { { "QUARTER",      nullptr, nullptr }, 1, false },  // viertel <nächste>
    /* :20 */ { { "MIN_10",       "TO",    "HALF"  }, 1, false },  // zehn vor halb
    /* :25 */ { { "MIN_5",        "TO",    "HALF"  }, 1, false },  // fünf vor halb
    /* :30 */ { { "HALF",         nullptr, nullptr }, 1, false },  // halb
    /* :35 */ { { "MIN_5",        "PAST",  "HALF"  }, 1, false },  // fünf nach halb
    /* :40 */ { { "MIN_10",       "PAST",  "HALF"  }, 1, false },  // zehn nach halb
    /* :45 */ { { "THREEQUARTER", nullptr, nullptr }, 1, false },  // dreiviertel <nächste>
    /* :50 */ { { "MIN_10",       "TO",    nullptr }, 1, false },  // zehn vor
    /* :55 */ { { "MIN_5",        "TO",    nullptr }, 1, false },  // fünf vor
  }
};

// The samples are the two steps where the dialects actually diverge (:15/:45
// and :20/:40). Showing 10:15 and 10:45 side by side is what lets a customer
// recognise their own way of speaking without knowing the word "dialect".
// Index 0 is the fallback when nothing is stored.
const ClockDialect DIALECTS_DE_50x50_V1[] = {
  { "de-nord", "Hochdeutsch", "viertel nach zehn · viertel vor elf", &DE_RULES_STANDARD },
  { "de-sued", "Süd-Ost",     "viertel elf · dreiviertel elf",       &DE_RULES_SUED },
};
const size_t DIALECTS_DE_50x50_V1_COUNT =
    sizeof(DIALECTS_DE_50x50_V1) / sizeof(DIALECTS_DE_50x50_V1[0]);
