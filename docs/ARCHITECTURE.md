# Architettura di anOS 1.1

## Sequenza di boot

1. UEFI carica `EFI/BOOT/BOOTX64.EFI`.
2. Il bootstrap acquisisce Graphics Output Protocol e memory map.
3. `ExitBootServices()` trasferisce il controllo completo ad anOS.
4. Il kernel inizializza framebuffer e console.
5. Installa una IDT x86-64 con interrupt gate.
6. Rimappa il PIC e programma il PIT a 100 Hz.
7. Configura il controller PS/2 e abilita gli interrupt.
8. Avvia il loop della shell usando `hlt` tra un interrupt e l'altro.

## Interrupt

| Vettore | Sorgente | Handler |
| --- | --- | --- |
| 32 | IRQ0 / PIT | Incrementa il contatore monotono |
| 33 | IRQ1 / PS/2 | Traduce scancode e alimenta il ring buffer |

Gli handler sono compilati con soli registri generali e terminano tramite
`iretq`. Il lavoro grafico non viene svolto dentro l'IRQ: l'handler deposita il
carattere in un buffer SPSC e la shell lo consuma nel contesto principale.

## Confine freestanding

- nessuna libreria standard o syscall host;
- IDT, PIC, PIT e PS/2 sono gestiti direttamente;
- il framebuffer è scritto dal kernel;
- la seriale COM1 usa I/O port-mapped;
- attesa a basso consumo tramite istruzione `hlt`;
- compilazione con `-ffreestanding`, `-nostdlib`, senza eccezioni e RTTI.

## Componenti

| File | Responsabilità |
| --- | --- |
| `src/uefi.hpp` | Tipi e protocolli UEFI minimi |
| `src/font.hpp` | Font bitmap 5×7 incorporato |
| `src/kernel.cpp` | Boot, IRQ, driver, console e shell |
| `scripts/build.sh` | Compilazione PE/COFF freestanding |
| `scripts/make_image.py` | Disco MBR/FAT32 riproducibile |
| `scripts/run.sh` | Macchina QEMU con firmware OVMF |

## Limiti intenzionali

La shell gira ancora in ring 0 ed è parte del kernel. Non esistono ancora
isolamento dei processi, heap dinamico, filesystem montato o programmi esterni.
Questi confini sono il prossimo passaggio architetturale.

