import json
import os

from SCons.Script import Import

Import("env")


def _load_product_config(env):
    project_dir = env["PROJECT_DIR"]
    pio_env = env.get("PIOENV")
    if not pio_env:
        return None
    product_json = os.path.join(project_dir, "products", pio_env, "product.json")
    if not os.path.isfile(product_json):
        return None
    try:
        with open(product_json, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except Exception as exc:
        print("grid_filter: failed to read", product_json, "->", exc)
        return None


GRID_MAP = {
    "nl_v4": ("ENABLE_GRID_NL_V4", "grid_variants/nl_v4.cpp"),
    "nl_50x50_v3": ("ENABLE_GRID_NL_50X50_V3", "grid_variants/nl_50x50_v3.cpp"),
    "nl_55x50_logo_v1": ("ENABLE_GRID_NL_55X50_LOGO_V1", "grid_variants/nl_55x50_logo_v1.cpp"),
    "nl_100x100_logo_v1": ("ENABLE_GRID_NL_100X100_LOGO_V1", "grid_variants/nl_100x100_logo_v1.cpp"),
    "nl_20x20_v1": ("ENABLE_GRID_NL_20X20_V1", "grid_variants/nl_20x20_v1.cpp"),
}


config = _load_product_config(env)
if config:
    grids = config.get("grids")
    if grids:
        src_filter = ["+<*>", "-<grid_variants/*>"]
        defines = []
        for grid in grids:
            mapping = GRID_MAP.get(grid)
            if not mapping:
                print("grid_filter: unknown grid", grid)
                continue
            macro, src = mapping
            defines.append((macro, 1))
            src_filter.append(f"+<{src}>")
        if defines:
            env.Replace(SRC_FILTER=src_filter)
            env.Append(CPPDEFINES=defines)
            print(f"grid_filter: enabled grids: {grids}")
            print(f"grid_filter: defines: {[d[0] for d in defines]}")
            print(f"grid_filter: src_filter: {src_filter}")
    else:
        print("grid_filter: no grids defined in product.json, all grids enabled")
else:
    print(f"grid_filter: no product.json found for {env.get('PIOENV')}, all grids enabled")