# anOS 1.0

Primo sistema operativo sperimentale del progetto anSofts: kernel x86-64
freestanding avviabile su firmware UEFI.

La versione 1.0 individua il framebuffer e la memoria tramite UEFI, chiama
`ExitBootServices()`, quindi continua senza un sistema operativo sottostante e
disegna direttamente la propria schermata. QEMU viene usato esclusivamente come
PC virtuale di prova.

## Requisiti

- Windows 10/11 con WSL2 e Ubuntu;
- supporto WSLg per vedere la finestra grafica;
- circa 1 GB libero per toolchain e build.

## Preparazione su Windows

Aprire PowerShell come amministratore, se WSL non è già installato:

```powershell
wsl --install -d Ubuntu
```

Riavviare se richiesto, aprire Ubuntu e raggiungere la cartella del progetto.
Per un percorso Windows, ad esempio:

```bash
cd /mnt/c/Users/Anthony/source/repos/anOS-1.0
chmod +x scripts/*.sh
./scripts/setup-wsl.sh
```

## Compilazione e avvio

```bash
make run
```

Vengono prodotti:

- `build/BOOTX64.EFI`: applicazione/kernel UEFI;
- `build/anOS-1.0.img`: immagine FAT32 avviabile;
- `build/esp/`: contenuto della EFI System Partition.

Per il solo log seriale, senza finestra grafica:

```bash
make run-headless
```

Il log corretto comprende:

```text
anOS 1.0: ingresso UEFI
anOS 1.0: ExitBootServices OK, kernel autonomo
anOS 1.0: framebuffer disegnato, arresto CPU
```

QEMU rimane aperto perché il kernel arresta intenzionalmente la CPU; chiuderlo
con `Ctrl+C` nel terminale.

## Verifica statica

```bash
make check
```

## Pulizia

```bash
make clean
```

## Roadmap

1. IDT, interrupt e timer;
2. allocatore della memoria fisica;
3. input da tastiera;
4. console e shell interattiva;
5. filesystem e caricamento programmi;
6. processi user-mode e scheduler;
7. rete e interfaccia grafica evoluta.

## Licenza

MIT, copyright 2026 Anthony Alessio Tralongo e anSofts.

