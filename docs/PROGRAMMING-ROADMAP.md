# Programmi e giochi per anOS

## Linguaggi iniziali

La prima SDK di anOS userà due livelli:

- **C** per ABI, syscall e libreria di sistema: semplice, stabile e facilmente
  richiamabile da altri linguaggi;
- **C++** per applicazioni e giochi: classi e astrazioni senza imporre un
  garbage collector o un runtime esterno.

Assembly x86-64 resterà confinato a context switch, syscall e pochi driver.
Rust potrà diventare un linguaggio ufficiale quando ABI e toolchain saranno
stabili. Lua è un buon candidato futuro per scripting di giochi e applicazioni.

## Formato applicativo previsto

I programmi non saranno compilati dentro il kernel. La direzione prevista è:

1. eseguibili **ELF64** x86-64;
2. processi separati in ring 3;
3. ABI C di anOS;
4. syscall documentate;
5. libreria `libanos` statica;
6. cross-compiler con target futuro `x86_64-unknown-anos`.

## Requisiti prima del primo programma

- allocatore fisico e virtuale;
- heap del kernel;
- paging e isolamento ring 3;
- scheduler e context switch;
- loader ELF64;
- filesystem;
- syscall per console, memoria, file, input e tempo.

## Requisiti aggiuntivi per i giochi

- doppio buffer grafico e primitive 2D;
- mouse e input non bloccante;
- timer ad alta precisione;
- audio PCM;
- caricamento di immagini, mappe e font;
- game loop con frame pacing;
- in seguito, porting di una parte compatibile di SDL.

Il primo gioco realistico sarà 2D e scritto in C++ usando una piccola libreria
nativa anOS. Pong, Breakout o Snake saranno ottimi test di grafica, input,
temporizzazione e audio senza richiedere subito una GPU 3D completa.

