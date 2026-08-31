set -euo pipefail

OUT_FILE="shaders.h"
SLANGC_BIN="${SLANGC:-slangc}"
TARGET="spirv"

usage() {
    echo "Usage: $0 [-o out_file.h] [-c slangc_path] shader1.slang [shader2.slang ...]" >&2
    exit 1
}

while getopts ":o:c:h" opt; do
    case "$opt" in
        o) OUT_FILE="$OPTARG" ;;
        c) SLANGC_BIN="$OPTARG" ;;
        h) usage ;;
        \?) echo "Unknown option: -$OPTARG" >&2; usage ;;
        :)  echo "Option -$OPTARG requires an argument" >&2; usage ;;
    esac
done
shift $((OPTIND - 1))

[[ $# -eq 0 ]] && { echo "Error: no input .slang files given." >&2; usage; }

if ! command -v "$SLANGC_BIN" >/dev/null 2>&1; then
    echo "Error: '$SLANGC_BIN' not found on PATH. Set SLANGC=/path/to/slangc or use -c." >&2
    exit 1
fi

out_dir="$(dirname "$OUT_FILE")"
mkdir -p "$out_dir"

tmp_body="$(mktemp)"
trap 'rm -f "$tmp_body"' EXIT

for src in "$@"; do
    if [[ ! -f "$src" ]]; then
        echo "Error: file not found: $src" >&2
        exit 1
    fi

    base="$(basename "$src")"
    base="${base%.*}"
    ident="$(echo "$base" | sed -E 's/[^A-Za-z0-9_]/_/g')"

    echo "Compiling $src -> ${ident}_spirv[]"

    spv_file="$(mktemp --suffix=.spv)"

    "$SLANGC_BIN" "$src" \
        -target "$TARGET" \
        -fvk-use-entrypoint-name \
        -o "$spv_file"

    python3 - "$spv_file" "$ident" >> "$tmp_body" <<'PYEOF'
import sys, struct

spv_path, ident = sys.argv[1], sys.argv[2]

with open(spv_path, "rb") as f:
    data = f.read()

if len(data) % 4 != 0:
    sys.exit(f"Error: SPIR-V file size {len(data)} is not a multiple of 4 bytes")

words = struct.unpack(f"<{len(data)//4}I", data)

print(f"static const uint32_t {ident}_spirv[{len(words)}] = {{")
for i in range(0, len(words), 8):
    chunk = words[i:i+8]
    line = ", ".join(f"0x{w:08x}" for w in chunk)
    print(f"    {line},")
print("};")
print(f"static const uint32_t {ident}_spirv_size = {len(data)}; // bytes")
print()
PYEOF

    rm -f "$spv_file"
done

{
    echo "#pragma once"
    echo "#include <stdint.h>"
    echo
    cat "$tmp_body"
} > "$OUT_FILE"

echo "Done. Wrote $OUT_FILE"