"""
Grid filter pre-build script (legacy ESP32 line).

Selects which grid_variant sources compile into a per-product firmware. The
PlatformIO env name (PIOENV) is the product directory name, so this reads
products/<PIOENV>/product.json, takes its "grids" array, and for each entry:

  * defines the matching ENABLE_GRID_* macro (consumed by grid_layout.cpp), and
  * whitelists the matching grid_variants/<name>.cpp in SRC_FILTER

so only the grids a product actually ships get compiled and linked. Everything
else in src/grid_variants is filtered out. grid_layout.cpp falls back to
enabling every grid when no ENABLE_GRID_* macro is defined (the native test
env and any env without a product.json), so this script only ever narrows.

This is the legacy counterpart of the nextgen tools/set_grid_filter.py. The
only real difference is GRID_MAP: the legacy line has its own set of grids
(nl_v1..v4, nl_50x50_v1..v3, the two logo grids, nl_20x20_v0/v1) and no
bootstrap sources, so there is no bootstrap exclusion here.
"""

import json
import os
import sys

from SCons.Script import Import

Import("env")


def _fail(message):
    # Abort the build rather than fall through to a permissive default. A
    # product that silently loses a grid variant still links and still boots,
    # it just cannot speak a language it was supposed to ship, which nothing
    # downstream catches. Failing here is the only cheap place to notice.
    print(f"grid_filter: ERROR: {message}", file=sys.stderr)
    sys.exit(1)


def _load_product_config(env):
    project_dir = env["PROJECT_DIR"]
    pio_env = env.get("PIOENV")
    if not pio_env:
        return None
    product_json = os.path.join(project_dir, "products", pio_env, "product.json")
    if not os.path.isfile(product_json):
        # No product.json at all is the native test env (or any env without a
        # product dir), which deliberately compiles every variant. Only a
        # *present but broken* file is an error.
        return None
    try:
        with open(product_json, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except Exception as exc:
        _fail(f"failed to read {product_json} -> {exc}")


# grid name (as written in product.json) -> (macro consumed by grid_layout.cpp,
# grid_variants source relative to src/). Keep the macro names in exact sync
# with the ENABLE_GRID_* guards in src/grid_layout.cpp.
GRID_MAP = {
    "nl_v1": ("ENABLE_GRID_NL_V1", "grid_variants/nl_v1.cpp"),
    "nl_v2": ("ENABLE_GRID_NL_V2", "grid_variants/nl_v2.cpp"),
    "nl_v3": ("ENABLE_GRID_NL_V3", "grid_variants/nl_v3.cpp"),
    "nl_v4": ("ENABLE_GRID_NL_V4", "grid_variants/nl_v4.cpp"),
    "nl_50x50_v1": ("ENABLE_GRID_NL_50X50_V1", "grid_variants/nl_50x50_v1.cpp"),
    "nl_50x50_v2": ("ENABLE_GRID_NL_50X50_V2", "grid_variants/nl_50x50_v2.cpp"),
    "nl_50x50_v3": ("ENABLE_GRID_NL_50X50_V3", "grid_variants/nl_50x50_v3.cpp"),
    "nl_55x50_logo_v1": ("ENABLE_GRID_NL_55X50_LOGO_V1", "grid_variants/nl_55x50_logo_v1.cpp"),
    "nl_100x100_logo_v1": ("ENABLE_GRID_NL_100X100_LOGO_V1", "grid_variants/nl_100x100_logo_v1.cpp"),
    "nl_20x20_v0": ("ENABLE_GRID_NL_20X20_V0", "grid_variants/nl_20x20_v0.cpp"),
    "nl_20x20_v1": ("ENABLE_GRID_NL_20X20_V1", "grid_variants/nl_20x20_v1.cpp"),
}


config = _load_product_config(env)
if config:
    grids = config.get("grids")
    if grids:
        src_filter = ["+<*>", "-<grid_variants/*>"]
        defines = []

        # Validate every entry before touching the build, so a typo can never
        # half-apply: the failure mode being guarded against is a product that
        # builds and boots but is quietly missing one of its languages.
        unknown = [g for g in grids if g not in GRID_MAP]
        if unknown:
            _fail(
                f"unknown grid(s) {unknown} in products/{env.get('PIOENV')}/product.json; "
                f"known grids are {sorted(GRID_MAP)}"
            )

        for grid in grids:
            macro, src = GRID_MAP[grid]
            defines.append((macro, 1))
            src_filter.append(f"+<{src}>")

        env.Replace(SRC_FILTER=src_filter)
        env.Append(CPPDEFINES=defines)
        print(f"grid_filter: enabled grids: {grids}")
        print(f"grid_filter: defines: {[d[0] for d in defines]}")
        print(f"grid_filter: src_filter: {src_filter}")
    else:
        print("grid_filter: no grids defined in product.json, all grids enabled")
else:
    print(f"grid_filter: no product.json found for {env.get('PIOENV')}, all grids enabled")
