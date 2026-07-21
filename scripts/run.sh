#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
IMAGE_FILE="$BUILD_DIR/anOS-1.1.img"
HEADLESS="${1:-}"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    echo "Errore: QEMU non installato. Esegui: ./scripts/setup-wsl.sh" >&2
    exit 1
fi

find_firmware() {
    local code vars
    while IFS='|' read -r code vars; do
        if [[ -f "$code" && -f "$vars" ]]; then
            printf '%s|%s' "$code" "$vars"
            return 0
        fi
    done <<'EOF'
/usr/share/OVMF/OVMF_CODE_4M.fd|/usr/share/OVMF/OVMF_VARS_4M.fd
/usr/share/OVMF/OVMF_CODE.fd|/usr/share/OVMF/OVMF_VARS.fd
/usr/share/edk2/x64/OVMF_CODE.fd|/usr/share/edk2/x64/OVMF_VARS.fd
EOF
    return 1
}

FIRMWARE_PAIR="$(find_firmware || true)"
if [[ -z "$FIRMWARE_PAIR" ]]; then
    echo "Errore: firmware OVMF non trovato. Esegui: ./scripts/setup-wsl.sh" >&2
    exit 1
fi
IFS='|' read -r OVMF_CODE OVMF_VARS_TEMPLATE <<< "$FIRMWARE_PAIR"
OVMF_VARS="$BUILD_DIR/OVMF_VARS.fd"
cp "$OVMF_VARS_TEMPLATE" "$OVMF_VARS"

QEMU_ARGS=(
    -machine q35
    -m 512M
    -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
    -drive "if=pflash,format=raw,file=$OVMF_VARS"
    -drive "format=raw,file=$IMAGE_FILE"
    -serial stdio
    -no-reboot
    -no-shutdown
)

if [[ "$HEADLESS" == "--headless" ]]; then
    QEMU_ARGS+=( -display none )
else
    QEMU_ARGS+=( -display gtk )
fi

exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
