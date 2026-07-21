# Architettura di anOS 1.0

## Confine freestanding

`BOOTX64.EFI` viene caricato dal firmware UEFI. La fase iniziale individua il
Graphics Output Protocol, copia i dati del framebuffer, ottiene la mappa della
memoria e invoca `ExitBootServices()`.

Dopo il successo di `ExitBootServices()`:

- non vengono più chiamati servizi di boot UEFI;
- il framebuffer è scritto direttamente dal kernel;
- il log usa direttamente la porta seriale COM1;
- nessuna libreria o syscall di Windows/Linux è collegata;
- la CPU viene arrestata con istruzioni x86-64 `cli` e `hlt`.

## Componenti

| File | Responsabilità |
| --- | --- |
| `src/uefi.hpp` | Tipi, tabelle e protocolli UEFI minimi |
| `src/font.hpp` | Font bitmap 5x7 incorporato |
| `src/kernel.cpp` | Bootstrap, seriale, memoria e framebuffer |
| `scripts/build.sh` | Compilazione PE/COFF |
| `scripts/make_image.py` | Disco MBR/FAT32 riproducibile senza tool esterni |
| `scripts/run.sh` | Avvio del PC virtuale QEMU/OVMF |

## Limitazioni intenzionali della 1.0

anOS 1.0 è il primo milestone tecnico: non possiede ancora interrupt propri,
scheduler, allocatore dinamico, filesystem montato o driver per tastiera. Questi
elementi formeranno i milestone successivi senza cambiare il confine
freestanding già stabilito.
