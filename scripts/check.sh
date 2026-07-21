#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EFI_FILE="$ROOT_DIR/build/BOOTX64.EFI"
IMAGE_FILE="$ROOT_DIR/build/anOS-1.1.img"

[[ -f "$EFI_FILE" ]] || { echo "Manca $EFI_FILE: esegui make" >&2; exit 1; }
[[ -f "$IMAGE_FILE" ]] || { echo "Manca $IMAGE_FILE: esegui make" >&2; exit 1; }

file "$EFI_FILE"
file "$IMAGE_FILE"

if command -v objdump >/dev/null 2>&1; then
    objdump -p "$EFI_FILE" | grep -E 'Subsystem|AddressOfEntryPoint' || true
fi

echo "Controlli statici completati. Per il test completo: make run"
