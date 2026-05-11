#!/usr/bin/env python3
"""Generate data/ral-classic.json from a TEC adsklib.

The TEC `.adsklib` (Autodesk material library) supplies the curated subset of
RAL Classic codes that TEC actually offers, plus Dutch color names — the
hex values themselves are stored in a proprietary binary stream we don't
parse. The canonical sRGB hex values are inlined below (sourced from
Wikipedia's List_of_RAL_colours, snapshot 2026-05-11).

Output schema:
    { "RAL 1000": { "hex": "#CDBA88", "nl": "Groenbeige", "en": "Green beige" },
      ... }
keyed by "RAL <code>", sorted by code. Codes present in the adsklib but
absent from the canonical hex table are dropped with a warning.

Usage:
    python3 tools/extract_ral_palette.py \\
        dist/TEC_RAL_kleuren.adsklib \\
        data/ral-classic.json
"""
from __future__ import annotations
import json
import re
import sys
import zipfile
from pathlib import Path


# Canonical RAL Classic sRGB approximations. The table is the authoritative
# source for the *hex value*; the TEC adsklib decides which subset of these
# codes the picker actually exposes and supplies the Dutch label per code.
RAL_CLASSIC_HEX = """\
RAL 1000 | #CDBA88 | Green beige
RAL 1001 | #D0B084 | Beige
RAL 1002 | #D2AA6D | Sand yellow
RAL 1003 | #F9A800 | Signal yellow
RAL 1004 | #E49E00 | Golden yellow
RAL 1005 | #CB8E00 | Honey yellow
RAL 1006 | #E29000 | Maize yellow
RAL 1007 | #E88C00 | Daffodil yellow
RAL 1011 | #AF804F | Brown beige
RAL 1012 | #DDAF27 | Lemon yellow
RAL 1013 | #E3D9C6 | Oyster white
RAL 1014 | #DDC49A | Ivory
RAL 1015 | #E6D2B5 | Light ivory
RAL 1016 | #F1DD38 | Sulfur yellow
RAL 1017 | #F6A950 | Saffron yellow
RAL 1018 | #FACA30 | Zinc yellow
RAL 1019 | #A48F7A | Grey beige
RAL 1020 | #A08F65 | Olive yellow
RAL 1021 | #F6B600 | Colza yellow
RAL 1023 | #F7B500 | Traffic yellow
RAL 1024 | #BA8F4C | Ochre yellow
RAL 1026 | #FFFF00 | Luminous yellow
RAL 1027 | #A77F0E | Curry
RAL 1028 | #FF9B00 | Melon yellow
RAL 1032 | #E2A300 | Broom yellow
RAL 1033 | #F99A1C | Dahlia yellow
RAL 1034 | #EB9C52 | Pastel yellow
RAL 1035 | #908370 | Pearl beige
RAL 1036 | #80643F | Pearl gold
RAL 1037 | #F09200 | Sun yellow
RAL 2000 | #DD7907 | Yellow orange
RAL 2001 | #BE4E20 | Red orange
RAL 2002 | #C63927 | Vermilion
RAL 2003 | #FA842B | Pastel orange
RAL 2004 | #E75B12 | Pure orange
RAL 2005 | #FF2300 | Luminous orange
RAL 2007 | #FFA421 | Luminous bright orange
RAL 2008 | #F3752C | Bright red orange
RAL 2009 | #E15501 | Traffic orange
RAL 2010 | #D4652F | Signal orange
RAL 2011 | #EC7C25 | Deep orange
RAL 2012 | #DB6A50 | Salmon orange
RAL 2013 | #954527 | Pearl orange
RAL 2017 | #FA4402 | RAL orange
RAL 3000 | #AB2524 | Flame red
RAL 3001 | #A02128 | Signal red
RAL 3002 | #A1232B | Carmine red
RAL 3003 | #8D1D2C | Ruby red
RAL 3004 | #701F29 | Purple red
RAL 3005 | #5E2028 | Wine red
RAL 3007 | #402225 | Black red
RAL 3009 | #703731 | Oxide red
RAL 3011 | #7E292C | Brown red
RAL 3012 | #CB8D73 | Beige red
RAL 3013 | #9C322E | Tomato red
RAL 3014 | #D47479 | Antique pink
RAL 3015 | #E1A6AD | Light pink
RAL 3016 | #AC4034 | Coral red
RAL 3017 | #D3545F | Rose
RAL 3018 | #D14152 | Strawberry red
RAL 3020 | #C1121C | Traffic red
RAL 3022 | #D56D56 | Salmon pink
RAL 3024 | #F70000 | Luminous red
RAL 3026 | #FF0000 | Luminous bright red
RAL 3027 | #B42041 | Raspberry red
RAL 3028 | #E72512 | Pure red
RAL 3031 | #AC323B | Orient red
RAL 3032 | #711521 | Pearl ruby red
RAL 3033 | #B24C43 | Pearl pink
RAL 4001 | #8A5A83 | Red lilac
RAL 4002 | #933D50 | Red violet
RAL 4003 | #D15B8F | Heather violet
RAL 4004 | #691639 | Claret violet
RAL 4005 | #83639D | Blue lilac
RAL 4006 | #992572 | Traffic purple
RAL 4007 | #4A203B | Purple violet
RAL 4008 | #904684 | Signal violet
RAL 4009 | #A38995 | Pastel violet
RAL 4010 | #C63678 | Telemagenta
RAL 4011 | #8773A1 | Pearl violet
RAL 4012 | #6B6880 | Pearl blackberry
RAL 5000 | #384C70 | Violet blue
RAL 5001 | #1F4764 | Green blue
RAL 5002 | #2B2C7C | Ultramarine blue
RAL 5003 | #2A3756 | Sapphire blue
RAL 5004 | #1D1F2A | Black blue
RAL 5005 | #154889 | Signal blue
RAL 5007 | #41678D | Brilliant blue
RAL 5008 | #313C48 | Grey blue
RAL 5009 | #2E5978 | Azure blue
RAL 5010 | #13447C | Gentian blue
RAL 5011 | #232C3F | Steel blue
RAL 5012 | #3481B8 | Light blue
RAL 5013 | #232D53 | Cobalt blue
RAL 5014 | #6C7C98 | Pigeon blue
RAL 5015 | #2874B2 | Sky blue
RAL 5017 | #0E518D | Traffic blue
RAL 5018 | #21888F | Turquoise blue
RAL 5019 | #1A5784 | Capri blue
RAL 5020 | #0B4151 | Ocean blue
RAL 5021 | #07737A | Water blue
RAL 5022 | #2F2A5A | Night blue
RAL 5023 | #4D668E | Distant blue
RAL 5024 | #6A93B0 | Pastel blue
RAL 5025 | #296478 | Pearl Gentian blue
RAL 5026 | #102C54 | Pearl night blue
RAL 6000 | #327662 | Patina green
RAL 6001 | #28713E | Emerald green
RAL 6002 | #276235 | Leaf green
RAL 6003 | #4B573E | Olive green
RAL 6004 | #0E4243 | Blue green
RAL 6005 | #0F4336 | Moss green
RAL 6006 | #40433B | Grey olive
RAL 6007 | #283424 | Bottle green
RAL 6008 | #35382E | Brown green
RAL 6009 | #26392F | Fir green
RAL 6010 | #3E753B | Grass green
RAL 6011 | #68825B | Reseda green
RAL 6012 | #31403D | Black green
RAL 6013 | #797C5A | Reed green
RAL 6014 | #444337 | Yellow olive
RAL 6015 | #3D403A | Black olive
RAL 6016 | #026A52 | Turquoise green
RAL 6017 | #468641 | May green
RAL 6018 | #48A43F | Yellow green
RAL 6019 | #B7D9B1 | Pastel green
RAL 6020 | #354733 | Chrome green
RAL 6021 | #86A47C | Pale green
RAL 6022 | #3E3C32 | Brown olive
RAL 6024 | #008754 | Traffic green
RAL 6025 | #53753C | Fern green
RAL 6026 | #005D52 | Opal green
RAL 6027 | #81C0BB | Light green
RAL 6028 | #2D5546 | Pine green
RAL 6029 | #007243 | Mint green
RAL 6032 | #0F8558 | Signal green
RAL 6033 | #478A84 | Mint turquoise
RAL 6034 | #7FB0B2 | Pastel turquoise
RAL 6035 | #1B542C | Pearl green
RAL 6036 | #005D4C | Pearl opal green
RAL 6037 | #25E712 | Pure green
RAL 6038 | #00F700 | Luminous green
RAL 6039 | #1B7236 | Fibrous green
RAL 7000 | #7E8B92 | Squirrel grey
RAL 7001 | #8F999F | Silver grey
RAL 7002 | #817F68 | Olive grey
RAL 7003 | #7A7B6D | Moss grey
RAL 7004 | #9EA0A1 | Signal grey
RAL 7005 | #6B716F | Mouse grey
RAL 7006 | #756F61 | Beige grey
RAL 7008 | #746643 | Khaki grey
RAL 7009 | #5B6259 | Green grey
RAL 7010 | #575D57 | Tarpaulin grey
RAL 7011 | #555D61 | Iron grey
RAL 7012 | #596163 | Basalt grey
RAL 7013 | #555548 | Brown grey
RAL 7015 | #51565C | Slate grey
RAL 7016 | #373F43 | Anthracite grey
RAL 7021 | #2E3234 | Black grey
RAL 7022 | #4B4D46 | Umbra grey
RAL 7023 | #818479 | Concrete grey
RAL 7024 | #474A50 | Graphite grey
RAL 7026 | #374447 | Granite grey
RAL 7030 | #939388 | Stone grey
RAL 7031 | #5D6970 | Blue grey
RAL 7032 | #B9B9A8 | Pebble grey
RAL 7033 | #818979 | Cement grey
RAL 7034 | #939176 | Yellow grey
RAL 7035 | #CBD0CC | Light grey
RAL 7036 | #9A9697 | Platinum grey
RAL 7037 | #7C7F7E | Dusty grey
RAL 7038 | #B4B8B0 | Agate grey
RAL 7039 | #6B695F | Quartz grey
RAL 7040 | #9DA3A6 | Window grey
RAL 7042 | #8F9695 | Traffic grey A
RAL 7043 | #4E5451 | Traffic grey B
RAL 7044 | #BDBDB2 | Silk grey
RAL 7045 | #91969A | Telegrey 1
RAL 7046 | #82898E | Telegrey 2
RAL 7047 | #CFD0CF | Telegrey 4
RAL 7048 | #888175 | Pearl mouse grey
RAL 8000 | #887142 | Green brown
RAL 8001 | #9C6B30 | Ochre brown
RAL 8002 | #7B5141 | Signal brown
RAL 8003 | #80542F | Clay brown
RAL 8004 | #8F4E35 | Copper brown
RAL 8007 | #6F4A2F | Fawn brown
RAL 8008 | #6F4F28 | Olive brown
RAL 8011 | #5A3A29 | Nut brown
RAL 8012 | #673831 | Red brown
RAL 8014 | #49392D | Sepia brown
RAL 8015 | #633A34 | Chestnut brown
RAL 8016 | #4C2F26 | Mahogany brown
RAL 8017 | #44322D | Chocolate brown
RAL 8019 | #3F3A3A | Grey brown
RAL 8022 | #211F20 | Black brown
RAL 8023 | #A65E2F | Orange brown
RAL 8024 | #79553C | Beige brown
RAL 8025 | #755C49 | Pale brown
RAL 8028 | #4E3B2B | Terra brown
RAL 8029 | #773C27 | Pearl copper
RAL 9001 | #EFEBDC | Cream
RAL 9002 | #DDDED4 | Grey white
RAL 9003 | #F4F8F4 | Signal white
RAL 9004 | #2E3032 | Signal black
RAL 9005 | #0A0A0D | Jet black
RAL 9006 | #A5A8A6 | White aluminium
RAL 9007 | #8F8F8C | Grey aluminium
RAL 9010 | #F7F9EF | Pure white
RAL 9011 | #1C1C1C | Graphite black
RAL 9012 | #FFFDE6 | Clean room white
RAL 9016 | #F7FBF5 | Traffic white
RAL 9017 | #2A2D2F | Traffic black
RAL 9018 | #CFD3CD | Papyrus white
RAL 9022 | #858583 | Pearl light grey
RAL 9023 | #7E8182 | Pearl dark grey
"""


def extract_tec_names(adsklib_path: Path) -> dict[str, str]:
    """Return {"1000": "Groenbeige", ...} from a TEC adsklib."""
    with zipfile.ZipFile(adsklib_path) as zf:
        # The asset properties live in <guid>/AssetData/InstanceProperties.bin.
        # There's exactly one per library; just pick the first match.
        target = next(
            n for n in zf.namelist() if n.endswith("/InstanceProperties.bin")
        )
        data = zf.read(target)

    out: dict[str, str] = {}
    # Match "TEC RAL\d{4} <dutch-name>" or "RAL\d{4} <dutch-name>" — both
    # forms appear in the binary. Decode each captured name as UTF-8 so
    # ë/è etc. survive; the binary mixes UTF-8 and latin-1 in places.
    for m in re.finditer(rb"(?:TEC )?RAL(\d{4}) ([^\x00-\x1f]{1,80})", data):
        code = m.group(1).decode("ascii")
        raw = m.group(2)
        try:
            name = raw.decode("utf-8")
        except UnicodeDecodeError:
            name = raw.decode("latin-1")
        # Some entries appear with "(1)" / "(2)" suffixes (multiple aspect
        # variants of the same material) — keep the bare name.
        name = re.sub(r"\s*\(\d+\)\s*$", "", name).strip()
        # Keep the longest variant if a code appears multiple times.
        if code not in out or len(name) > len(out[code]):
            out[code] = name
    return out


def parse_canonical_hex(text: str) -> dict[str, tuple[str, str]]:
    """Parse 'RAL NNNN | #RRGGBB | English Name' rows from the inlined table."""
    out: dict[str, tuple[str, str]] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 3:
            continue
        code = parts[0].removeprefix("RAL ").strip()
        out[code] = (parts[1], parts[2])
    return out


def main(adsklib: str, out_file: str) -> int:
    tec_names = extract_tec_names(Path(adsklib))
    canonical = parse_canonical_hex(RAL_CLASSIC_HEX)

    merged: dict[str, dict[str, str]] = {}
    missing: list[str] = []
    for code in sorted(tec_names):
        if code not in canonical:
            missing.append(code)
            continue
        hex_value, en_name = canonical[code]
        merged[f"RAL {code}"] = {
            "hex": hex_value,
            "nl": tec_names[code],
            "en": en_name,
        }

    Path(out_file).write_text(
        json.dumps(merged, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    print(f"Wrote {len(merged)} entries to {out_file}")
    if missing:
        print(
            f"WARN: {len(missing)} TEC code(s) not in canonical hex table, "
            f"dropped: {', '.join(missing)}",
            file=sys.stderr,
        )
    excluded = sorted(set(canonical) - set(tec_names))
    if excluded:
        print(
            f"Note: {len(excluded)} canonical RAL code(s) not shipped by "
            f"TEC, excluded by design: {', '.join(excluded)}",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
