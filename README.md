# anOS 1.1

Sistema operativo sperimentale x86-64 del progetto anSofts. anOS 1.1 aggiunge
interrupt hardware, timer, tastiera PS/2 e una shell grafica interattiva al
kernel UEFI freestanding introdotto con anOS 1.0.

Il kernel ottiene framebuffer e mappa della memoria tramite UEFI, chiama
`ExitBootServices()` e prosegue senza un sistema operativo sottostante. Da quel
momento gestisce direttamente IDT, PIC 8259, PIT, controller PS/2 e framebuffer.

## Novità della versione 1.1

- IDT x86-64 installata dal kernel;
- PIC rimappato: IRQ0 → vettore 32, IRQ1 → vettore 33;
- timer PIT programmato a 100 Hz;
- tastiera PS/2 interrupt-driven, con Shift, Caps Lock, Invio e Backspace;
- ring buffer per separare IRQ e shell;
- console grafica con scorrimento;
- shell interattiva e primi comandi;
- conteggio RAM corretto, senza includere MMIO e memoria runtime.

## Preparazione su Windows/WSL2

Da Ubuntu, nella cartella del progetto:

```bash
chmod +x scripts/*.sh
bash scripts/setup-wsl.sh
```

## Compilazione e avvio

```bash
make run
```

Vengono prodotti:

- `build/BOOTX64.EFI`: kernel UEFI;
- `build/anOS-1.1.img`: disco MBR/FAT32 avviabile;
- `build/esp/`: contenuto della EFI System Partition.

Se WSLg è già configurato, QEMU apre automaticamente la finestra grafica. Il
terminale ospitante mostra contemporaneamente il log seriale del kernel.

## Primi comandi

| Comando | Funzione |
| --- | --- |
| `help` | Mostra l'elenco integrato |
| `about` | Informazioni su anOS |
| `version` | Versione e architettura del kernel |
| `clear` / `cls` | Pulisce la console |
| `echo TESTO` | Stampa il testo indicato |
| `mem` | Mostra la RAM utilizzabile |
| `uptime` | Secondi trascorsi dal boot |
| `ticks` | Numero di interrupt PIT ricevuti |
| `irq` | Contatori IRQ0 e IRQ1 |
| `cpu` | Vendor della CPU tramite CPUID |
| `color cyan` | Testo ciano; anche `green`, `white`, `amber` |
| `reboot` | Riavvia la macchina tramite controller 8042 |
| `halt` | Disabilita gli interrupt e arresta la CPU |

La mappatura corrente della tastiera segue lo scancode set 1 in layout US. Le
lettere di una tastiera italiana coincidono; la localizzazione dei simboli sarà
aggiunta in una versione successiva.

## Controlli

```bash
make check
```

## Prossimi milestone

1. gestore delle eccezioni CPU e schermata panic;
2. allocatore di pagine e heap del kernel;
3. paging proprietario e processi ring 3;
4. filesystem e loader ELF64;
5. syscall e SDK applicativo C/C++;
6. mouse, audio e libreria grafica 2D;
7. primo gioco nativo anOS.

La strategia per programmi e giochi è descritta in
`docs/PROGRAMMING-ROADMAP.md`.

## Licenza

MIT, copyright 2026 Anthony Alessio Tralongo e anSofts.

