#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
    SUDO=""
else
    SUDO="sudo"
fi

$SUDO apt-get update
$SUDO apt-get install -y \
    clang lld make qemu-system-x86 ovmf file python3

echo "Ambiente pronto. Torna nella cartella del progetto ed esegui: make run"
