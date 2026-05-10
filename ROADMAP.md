# Roadmap

Park-it items: ideas surfaced during work but deferred. Not commitments — a
place to capture context so the idea isn't lost when the conversation ends.

## Color picker — extend input methods

Surfaced 2026-05-10. Today the v2 swatch palette + native "Custom…" picker
covers the common case; these are extensions for power users.

- **Hex text field** (`#RRGGBB` / `RRGGBB`). Cheapest to ship — server's
  `/setColor?color=…` already accepts any hex. ~30 lines of JS in
  `data/dashboard-v2.html` (and `data/admin-v2.html`) next to the
  "Custom…" button. Validate, then call existing `applyColor(hex)`.
- **RGB triplet** (`r,g,b` 0–255). Three small numeric fields, convert
  client-side to hex, reuse `/setColor`.
- **RAL Classic dropdown / search** (~213 codes). Ship a hardcoded
  `RAL → hex` table in JS (~5–8 KB minified). Scope to RAL Classic only;
  RAL Design (~1,825) and RAL Effect (490) are out of scope.

**Caveat to surface in the UI when RAL is added**: WS2812B addressable
LEDs have a narrower gamut than print/coating systems. Most RAL codes
won't reproduce accurately under emissive light — especially matte
coatings. Mark RAL input as "preview only — LED gamut differs from
coating" so customers don't expect an exact match.
