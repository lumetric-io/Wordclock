import json
import os
import sys

from SCons.Script import Import

Import("env")


def _fail(message):
    # Abort the build rather than fall through to a permissive default. A
    # product that silently loses a grid variant still links and still boots —
    # it just can't speak a language it was supposed to ship, which nothing
    # downstream catches. Failing here is the only place it is cheap to notice.
    print(f"grid_filter: ERROR: {message}", file=sys.stderr)
    sys.exit(1)


def _load_product_config(env):
    project_dir = env["PROJECT_DIR"]
    pio_env = env.get("PIOENV")
    if not pio_env:
        return None
    product_json = os.path.join(project_dir, "products", pio_env, "product.json")
    if not os.path.isfile(product_json):
        # No product.json at all is the native test env, which deliberately
        # compiles every variant. Only a *present but broken* file is an error.
        return None
    try:
        with open(product_json, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except Exception as exc:
        _fail(f"failed to read {product_json} -> {exc}")


GRID_MAP = {
    "nl_v4": ("ENABLE_GRID_NL_V4", "grid_variants/nl_v4.cpp"),
    "nl_50x50_v3": ("ENABLE_GRID_NL_50X50_V3", "grid_variants/nl_50x50_v3.cpp"),
    "nl_55x50_logo_v1": ("ENABLE_GRID_NL_55X50_LOGO_V1", "grid_variants/nl_55x50_logo_v1.cpp"),
    "nl_105x105_logo_v1": ("ENABLE_GRID_NL_105X105_LOGO_V1", "grid_variants/nl_105x105_logo_v1.cpp"),
    "nl_20x20_v1": ("ENABLE_GRID_NL_20X20_V1", "grid_variants/nl_20x20_v1.cpp"),
    "de_50x50_v1": ("ENABLE_GRID_DE_50X50_V1", "grid_variants/de_50x50_v1.cpp"),
}


config = _load_product_config(env)
if config:
    grids = config.get("grids")
    if grids:
        # Bootstrap sources define their own setup()/loop()/server, so they'd
        # collide with main.cpp if linked into a per-device build. Bootstrap
        # has its own (grids-less) env that builds those files explicitly via
        # platformio.env.ini, so it never reaches this branch.
        src_filter = ["+<*>", "-<grid_variants/*>", "-<bootstrap_*.cpp>"]
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