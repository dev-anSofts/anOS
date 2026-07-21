#!/usr/bin/env python3
"""Crea un disco MBR/FAT32 UEFI avviabile senza utility esterne."""

from __future__ import annotations

import math
import pathlib
import struct
import sys

SECTOR_SIZE = 512
IMAGE_SIZE = 64 * 1024 * 1024
PARTITION_START = 2048
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 1


def directory_entry(name: bytes, attributes: int, cluster: int, size: int = 0) -> bytes:
    if len(name) != 11:
        raise ValueError("Il nome FAT 8.3 deve occupare esattamente 11 byte")
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = attributes
    struct.pack_into("<H", entry, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Uso: {sys.argv[0]} BOOTX64.EFI anOS-1.0.img", file=sys.stderr)
        return 2

    efi_path = pathlib.Path(sys.argv[1])
    image_path = pathlib.Path(sys.argv[2])
    efi_data = efi_path.read_bytes()
    if not efi_data.startswith(b"MZ"):
        raise ValueError("BOOTX64.EFI non è un eseguibile PE/COFF valido")

    total_sectors = IMAGE_SIZE // SECTOR_SIZE
    partition_sectors = total_sectors - PARTITION_START

    fat_sectors = 1
    while True:
        data_sectors = partition_sectors - RESERVED_SECTORS - FAT_COUNT * fat_sectors
        cluster_count = data_sectors // SECTORS_PER_CLUSTER
        required = math.ceil((cluster_count + 2) * 4 / SECTOR_SIZE)
        if required == fat_sectors:
            break
        fat_sectors = required

    data_start = PARTITION_START + RESERVED_SECTORS + FAT_COUNT * fat_sectors
    file_clusters = math.ceil(len(efi_data) / (SECTOR_SIZE * SECTORS_PER_CLUSTER))
    first_file_cluster = 5

    image_path.parent.mkdir(parents=True, exist_ok=True)
    with image_path.open("w+b") as image:
        image.truncate(IMAGE_SIZE)

        # Master Boot Record con una partizione FAT32 LBA.
        mbr = bytearray(SECTOR_SIZE)
        partition = bytearray(16)
        partition[0] = 0x80
        partition[1:4] = b"\xFE\xFF\xFF"
        partition[4] = 0x0C
        partition[5:8] = b"\xFE\xFF\xFF"
        struct.pack_into("<II", partition, 8, PARTITION_START, partition_sectors)
        mbr[446:462] = partition
        mbr[510:512] = b"\x55\xAA"
        image.seek(0)
        image.write(mbr)

        # BIOS Parameter Block FAT32.
        boot = bytearray(SECTOR_SIZE)
        boot[0:3] = b"\xEB\x58\x90"
        boot[3:11] = b"ANOS1.0 "
        struct.pack_into("<H", boot, 11, SECTOR_SIZE)
        boot[13] = SECTORS_PER_CLUSTER
        struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
        boot[16] = FAT_COUNT
        struct.pack_into("<H", boot, 17, 0)
        struct.pack_into("<H", boot, 19, 0)
        boot[21] = 0xF8
        struct.pack_into("<H", boot, 22, 0)
        struct.pack_into("<H", boot, 24, 63)
        struct.pack_into("<H", boot, 26, 255)
        struct.pack_into("<I", boot, 28, PARTITION_START)
        struct.pack_into("<I", boot, 32, partition_sectors)
        struct.pack_into("<I", boot, 36, fat_sectors)
        struct.pack_into("<H", boot, 40, 0)
        struct.pack_into("<H", boot, 42, 0)
        struct.pack_into("<I", boot, 44, 2)
        struct.pack_into("<H", boot, 48, 1)
        struct.pack_into("<H", boot, 50, 6)
        boot[64] = 0x80
        boot[66] = 0x29
        struct.pack_into("<I", boot, 67, 0xA1051000)
        boot[71:82] = b"ANOS       "
        boot[82:90] = b"FAT32   "
        boot[510:512] = b"\x55\xAA"

        fsinfo = bytearray(SECTOR_SIZE)
        struct.pack_into("<I", fsinfo, 0, 0x41615252)
        struct.pack_into("<I", fsinfo, 484, 0x61417272)
        struct.pack_into("<I", fsinfo, 488, 0xFFFFFFFF)
        struct.pack_into("<I", fsinfo, 492, first_file_cluster + file_clusters)
        struct.pack_into("<I", fsinfo, 508, 0xAA550000)

        for relative_sector, payload in ((0, boot), (1, fsinfo), (6, boot), (7, fsinfo)):
            image.seek((PARTITION_START + relative_sector) * SECTOR_SIZE)
            image.write(payload)

        # File Allocation Table: root, EFI, BOOT e contenuto BOOTX64.EFI.
        fat = bytearray(fat_sectors * SECTOR_SIZE)
        entries = [0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF, 0x0FFFFFFF, 0x0FFFFFFF]
        for index, value in enumerate(entries):
            struct.pack_into("<I", fat, index * 4, value)
        for index in range(file_clusters):
            cluster = first_file_cluster + index
            next_cluster = 0x0FFFFFFF if index == file_clusters - 1 else cluster + 1
            struct.pack_into("<I", fat, cluster * 4, next_cluster)
        for fat_index in range(FAT_COUNT):
            image.seek((PARTITION_START + RESERVED_SECTORS + fat_index * fat_sectors) * SECTOR_SIZE)
            image.write(fat)

        def write_cluster(cluster: int, payload: bytes) -> None:
            sector = data_start + (cluster - 2) * SECTORS_PER_CLUSTER
            image.seek(sector * SECTOR_SIZE)
            image.write(payload.ljust(SECTOR_SIZE * SECTORS_PER_CLUSTER, b"\0"))

        root = directory_entry(b"ANOS       ", 0x08, 0)
        root += directory_entry(b"EFI        ", 0x10, 3)
        write_cluster(2, root)

        efi_directory = directory_entry(b".          ", 0x10, 3)
        efi_directory += directory_entry(b"..         ", 0x10, 2)
        efi_directory += directory_entry(b"BOOT       ", 0x10, 4)
        write_cluster(3, efi_directory)

        boot_directory = directory_entry(b".          ", 0x10, 4)
        boot_directory += directory_entry(b"..         ", 0x10, 3)
        boot_directory += directory_entry(
            b"BOOTX64 EFI", 0x20, first_file_cluster, len(efi_data))
        write_cluster(4, boot_directory)

        for index in range(file_clusters):
            start = index * SECTOR_SIZE * SECTORS_PER_CLUSTER
            end = start + SECTOR_SIZE * SECTORS_PER_CLUSTER
            write_cluster(first_file_cluster + index, efi_data[start:end])

    print(f"Immagine UEFI creata: {image_path} ({IMAGE_SIZE // (1024 * 1024)} MiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

