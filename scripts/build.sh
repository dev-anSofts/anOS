#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
ESP_DIR="$BUILD_DIR/esp"
EFI_FILE="$BUILD_DIR/BOOTX64.EFI"

for command_name in clang++ lld-link python3; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Errore: manca '$command_name'. Esegui: ./scripts/setup-wsl.sh" >&2
        exit 1
    fi
done

mkdir -p "$BUILD_DIR" "$ESP_DIR/EFI/BOOT"

clang++ \
    --target=x86_64-pc-win32-coff \
    -std=c++20 \
    -O2 \
    -ffreestanding \
    -fno-builtin \
    -fno-exceptions \
    -fno-rtti \
    -fno-stack-protector \
    -fshort-wchar \
    -mno-red-zone \
    -mno-stack-arg-probe \
    -mgeneral-regs-only \
    -Wall -Wextra -Wpedantic -Werror \
    -c "$ROOT_DIR/src/kernel.cpp" \
    -o "$BUILD_DIR/kernel.obj"

lld-link \
    /subsystem:efi_application \
    /entry:efi_main \
    /machine:x64 \
    /dynamicbase \
    /nodefaultlib \
    /out:"$EFI_FILE" \
    "$BUILD_DIR/kernel.obj"

cp "$EFI_FILE" "$ESP_DIR/EFI/BOOT/BOOTX64.EFI"

IMAGE_FILE="$BUILD_DIR/anOS-1.1.img"
python3 "$ROOT_DIR/scripts/make_image.py" "$EFI_FILE" "$IMAGE_FILE"

echo "anOS 1.1 compilato con successo"
echo "EFI: $EFI_FILE"
echo "Immagine avviabile: $IMAGE_FILE"
