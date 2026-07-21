#include "uefi.hpp"
#include "font.hpp"

// Mantiene una relocation PE/COFF nel binario, così UEFI può caricare anOS
// a qualunque indirizzo fisico disponibile.
extern "C" __attribute__((used)) void* g_anos_relocation_anchor =
    reinterpret_cast<void*>(&g_anos_relocation_anchor);

namespace {

using namespace uefi;

constexpr uint32_t PIT_FREQUENCY_HZ = 100;
constexpr uint32_t KEY_BUFFER_SIZE = 256;
constexpr uint32_t MAX_TERMINAL_COLUMNS = 100;
constexpr uint32_t MAX_TERMINAL_ROWS = 40;

#define ISR_SAFE __attribute__((no_caller_saved_registers))

struct Framebuffer {
    volatile uint32_t* base;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    EFI_GRAPHICS_PIXEL_FORMAT format;
};

struct [[gnu::packed]] IdtEntry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};

struct [[gnu::packed]] IdtPointer {
    uint16_t limit;
    uint64_t base;
};

struct InterruptFrame {
    uint64_t instruction_pointer;
    uint64_t code_segment;
    uint64_t flags;
    uint64_t stack_pointer;
    uint64_t stack_segment;
};

struct TerminalCell {
    char character;
    uint32_t color;
};

struct Terminal {
    Framebuffer framebuffer;
    uint32_t origin_x;
    uint32_t origin_y;
    uint32_t columns;
    uint32_t rows;
    uint32_t cursor_column;
    uint32_t cursor_row;
    uint32_t scale;
    uint32_t cell_width;
    uint32_t cell_height;
    uint32_t background;
    uint32_t foreground;
};

alignas(16) uint8_t g_memory_map[128 * 1024];
alignas(16) IdtEntry g_idt[256];
TerminalCell g_terminal_cells[MAX_TERMINAL_COLUMNS * MAX_TERMINAL_ROWS];

volatile uint64_t g_timer_ticks = 0;
volatile uint64_t g_keyboard_interrupts = 0;
volatile uint32_t g_key_head = 0;
volatile uint32_t g_key_tail = 0;
volatile bool g_shift_pressed = false;
volatile bool g_caps_lock = false;
volatile bool g_extended_scancode = false;
char g_key_buffer[KEY_BUFFER_SIZE];

ISR_SAFE inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

ISR_SAFE inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void io_wait() {
    outb(0x80, 0);
}

void serial_init() {
    constexpr uint16_t port = 0x3F8;
    outb(port + 1, 0x00);
    outb(port + 3, 0x80);
    outb(port + 0, 0x03);
    outb(port + 1, 0x00);
    outb(port + 3, 0x03);
    outb(port + 2, 0xC7);
    outb(port + 4, 0x0B);
}

void serial_write(const char* text) {
    constexpr uint16_t port = 0x3F8;
    while (*text) {
        while ((inb(port + 5) & 0x20) == 0) {}
        outb(port, static_cast<uint8_t>(*text++));
    }
}

uint32_t pixel(const Framebuffer& fb, uint8_t red, uint8_t green, uint8_t blue) {
    if (fb.format == PixelRedGreenBlueReserved8BitPerColor) {
        return static_cast<uint32_t>(red) |
               (static_cast<uint32_t>(green) << 8) |
               (static_cast<uint32_t>(blue) << 16);
    }
    return static_cast<uint32_t>(blue) |
           (static_cast<uint32_t>(green) << 8) |
           (static_cast<uint32_t>(red) << 16);
}

void fill_rect(const Framebuffer& fb, uint32_t x, uint32_t y,
               uint32_t width, uint32_t height, uint32_t color) {
    if (x >= fb.width || y >= fb.height) return;
    if (x + width > fb.width) width = fb.width - x;
    if (y + height > fb.height) height = fb.height - y;
    for (uint32_t row = 0; row < height; ++row) {
        volatile uint32_t* line = fb.base + (y + row) * fb.stride + x;
        for (uint32_t column = 0; column < width; ++column) {
            line[column] = color;
        }
    }
}

void draw_char(const Framebuffer& fb, uint32_t x, uint32_t y, char character,
               uint32_t color, uint32_t scale) {
    const uint8_t* rows = anos::font::glyph(character);
    for (uint32_t row = 0; row < 7; ++row) {
        for (uint32_t column = 0; column < 5; ++column) {
            if ((rows[row] & (1U << (4U - column))) != 0) {
                fill_rect(fb, x + column * scale, y + row * scale,
                          scale, scale, color);
            }
        }
    }
}

void draw_text(const Framebuffer& fb, uint32_t x, uint32_t y, const char* text,
               uint32_t color, uint32_t scale) {
    const uint32_t advance = 6 * scale;
    while (*text) {
        draw_char(fb, x, y, *text++, color, scale);
        x += advance;
    }
}

[[noreturn]] void halt_forever() {
    asm volatile("cli");
    for (;;) asm volatile("hlt");
}

ISR_SAFE void key_buffer_push(char character) {
    const uint32_t head = g_key_head;
    const uint32_t next = (head + 1) % KEY_BUFFER_SIZE;
    if (next == g_key_tail) return;
    g_key_buffer[head] = character;
    asm volatile("" ::: "memory");
    g_key_head = next;
}

bool key_buffer_pop(char& character) {
    const uint32_t tail = g_key_tail;
    if (tail == g_key_head) return false;
    character = g_key_buffer[tail];
    asm volatile("" ::: "memory");
    g_key_tail = (tail + 1) % KEY_BUFFER_SIZE;
    return true;
}

ISR_SAFE char letter(char lower_case) {
    const bool upper_case = static_cast<bool>(g_shift_pressed) !=
                            static_cast<bool>(g_caps_lock);
    return upper_case ? static_cast<char>(lower_case - 'a' + 'A') : lower_case;
}

ISR_SAFE char translate_scancode(uint8_t scancode) {
    switch (scancode) {
        case 0x02: return g_shift_pressed ? '!' : '1';
        case 0x03: return g_shift_pressed ? '@' : '2';
        case 0x04: return g_shift_pressed ? '#' : '3';
        case 0x05: return g_shift_pressed ? '$' : '4';
        case 0x06: return g_shift_pressed ? '%' : '5';
        case 0x07: return g_shift_pressed ? '^' : '6';
        case 0x08: return g_shift_pressed ? '&' : '7';
        case 0x09: return g_shift_pressed ? '*' : '8';
        case 0x0A: return g_shift_pressed ? '(' : '9';
        case 0x0B: return g_shift_pressed ? ')' : '0';
        case 0x0C: return g_shift_pressed ? '_' : '-';
        case 0x0D: return g_shift_pressed ? '+' : '=';
        case 0x0E: return '\b';
        case 0x0F: return '\t';
        case 0x10: return letter('q');
        case 0x11: return letter('w');
        case 0x12: return letter('e');
        case 0x13: return letter('r');
        case 0x14: return letter('t');
        case 0x15: return letter('y');
        case 0x16: return letter('u');
        case 0x17: return letter('i');
        case 0x18: return letter('o');
        case 0x19: return letter('p');
        case 0x1A: return g_shift_pressed ? '{' : '[';
        case 0x1B: return g_shift_pressed ? '}' : ']';
        case 0x1C: return '\n';
        case 0x1E: return letter('a');
        case 0x1F: return letter('s');
        case 0x20: return letter('d');
        case 0x21: return letter('f');
        case 0x22: return letter('g');
        case 0x23: return letter('h');
        case 0x24: return letter('j');
        case 0x25: return letter('k');
        case 0x26: return letter('l');
        case 0x27: return g_shift_pressed ? ':' : ';';
        case 0x28: return g_shift_pressed ? '"' : '\'';
        case 0x29: return g_shift_pressed ? '~' : '`';
        case 0x2B: return g_shift_pressed ? '|' : '\\';
        case 0x2C: return letter('z');
        case 0x2D: return letter('x');
        case 0x2E: return letter('c');
        case 0x2F: return letter('v');
        case 0x30: return letter('b');
        case 0x31: return letter('n');
        case 0x32: return letter('m');
        case 0x33: return g_shift_pressed ? '<' : ',';
        case 0x34: return g_shift_pressed ? '>' : '.';
        case 0x35: return g_shift_pressed ? '?' : '/';
        case 0x39: return ' ';
        default: return 0;
    }
}

ISR_SAFE void pic_end_of_interrupt(uint8_t irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

__attribute__((interrupt)) void timer_interrupt_handler(InterruptFrame*) {
    g_timer_ticks = g_timer_ticks + 1;
    pic_end_of_interrupt(0);
}

__attribute__((interrupt)) void keyboard_interrupt_handler(InterruptFrame*) {
    const uint8_t scancode = inb(0x60);
    g_keyboard_interrupts = g_keyboard_interrupts + 1;

    if (scancode == 0xE0) {
        g_extended_scancode = true;
        pic_end_of_interrupt(1);
        return;
    }
    if (g_extended_scancode) {
        g_extended_scancode = false;
        pic_end_of_interrupt(1);
        return;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        g_shift_pressed = true;
    } else if (scancode == 0xAA || scancode == 0xB6) {
        g_shift_pressed = false;
    } else if (scancode == 0x3A) {
        g_caps_lock = !g_caps_lock;
    } else if ((scancode & 0x80) == 0) {
        const char character = translate_scancode(scancode);
        if (character != 0) key_buffer_push(character);
    }

    pic_end_of_interrupt(1);
}

template <typename Handler>
void set_idt_gate(uint8_t vector, Handler handler, uint16_t code_selector) {
    const uint64_t address = reinterpret_cast<uint64_t>(handler);
    IdtEntry& entry = g_idt[vector];
    entry.offset_low = static_cast<uint16_t>(address);
    entry.selector = code_selector;
    entry.ist = 0;
    entry.attributes = 0x8E;
    entry.offset_middle = static_cast<uint16_t>(address >> 16);
    entry.offset_high = static_cast<uint32_t>(address >> 32);
    entry.reserved = 0;
}

void idt_initialize() {
    for (auto& entry : g_idt) {
        entry = {};
    }

    uint16_t code_selector;
    asm volatile("mov %%cs, %0" : "=r"(code_selector));
    set_idt_gate(32, timer_interrupt_handler, code_selector);
    set_idt_gate(33, keyboard_interrupt_handler, code_selector);

    const IdtPointer pointer {
        static_cast<uint16_t>(sizeof(g_idt) - 1),
        reinterpret_cast<uint64_t>(&g_idt[0])
    };
    asm volatile("lidt %0" : : "m"(pointer));
}

void pic_initialize() {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    // Master: abilita IRQ0 (timer) e IRQ1 (tastiera). Slave tutto mascherato.
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);
}

void pit_initialize() {
    const uint16_t divisor = static_cast<uint16_t>(1193182U / PIT_FREQUENCY_HZ);
    outb(0x43, 0x36);
    outb(0x40, static_cast<uint8_t>(divisor & 0xFF));
    outb(0x40, static_cast<uint8_t>(divisor >> 8));
}

bool ps2_wait_input_clear() {
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        if ((inb(0x64) & 0x02) == 0) return true;
        asm volatile("pause");
    }
    return false;
}

bool ps2_wait_output_full() {
    for (uint32_t attempt = 0; attempt < 100000; ++attempt) {
        if ((inb(0x64) & 0x01) != 0) return true;
        asm volatile("pause");
    }
    return false;
}

void ps2_keyboard_initialize() {
    while ((inb(0x64) & 0x01) != 0) {
        static_cast<void>(inb(0x60));
    }

    if (!ps2_wait_input_clear()) return;
    outb(0x64, 0xAE);

    if (!ps2_wait_input_clear()) return;
    outb(0x64, 0x20);
    if (!ps2_wait_output_full()) return;
    uint8_t configuration = inb(0x60);
    configuration = static_cast<uint8_t>((configuration | 0x41) & ~0x10);

    if (!ps2_wait_input_clear()) return;
    outb(0x64, 0x60);
    if (!ps2_wait_input_clear()) return;
    outb(0x60, configuration);

    if (!ps2_wait_input_clear()) return;
    outb(0x60, 0xF4);
    if (ps2_wait_output_full()) static_cast<void>(inb(0x60));
}

void interrupts_initialize() {
    asm volatile("cli");
    idt_initialize();
    pic_initialize();
    pit_initialize();
    ps2_keyboard_initialize();
    asm volatile("sti");
}

uint32_t terminal_index(const Terminal& terminal, uint32_t column, uint32_t row) {
    return row * terminal.columns + column;
}

void terminal_draw_cell(const Terminal& terminal, uint32_t column, uint32_t row) {
    const TerminalCell& cell = g_terminal_cells[terminal_index(terminal, column, row)];
    const uint32_t x = terminal.origin_x + column * terminal.cell_width;
    const uint32_t y = terminal.origin_y + row * terminal.cell_height;
    fill_rect(terminal.framebuffer, x, y, terminal.cell_width,
              terminal.cell_height, terminal.background);
    draw_char(terminal.framebuffer, x, y + 2, cell.character, cell.color, terminal.scale);
}

void terminal_scroll(Terminal& terminal) {
    for (uint32_t row = 1; row < terminal.rows; ++row) {
        for (uint32_t column = 0; column < terminal.columns; ++column) {
            g_terminal_cells[terminal_index(terminal, column, row - 1)] =
                g_terminal_cells[terminal_index(terminal, column, row)];
            terminal_draw_cell(terminal, column, row - 1);
        }
    }
    for (uint32_t column = 0; column < terminal.columns; ++column) {
        TerminalCell& cell = g_terminal_cells[
            terminal_index(terminal, column, terminal.rows - 1)];
        cell = {' ', terminal.foreground};
        terminal_draw_cell(terminal, column, terminal.rows - 1);
    }
    terminal.cursor_row = terminal.rows - 1;
}

void terminal_newline(Terminal& terminal) {
    terminal.cursor_column = 0;
    ++terminal.cursor_row;
    if (terminal.cursor_row >= terminal.rows) terminal_scroll(terminal);
}

void terminal_clear(Terminal& terminal) {
    fill_rect(terminal.framebuffer, terminal.origin_x, terminal.origin_y,
              terminal.columns * terminal.cell_width,
              terminal.rows * terminal.cell_height, terminal.background);
    for (uint32_t row = 0; row < terminal.rows; ++row) {
        for (uint32_t column = 0; column < terminal.columns; ++column) {
            g_terminal_cells[terminal_index(terminal, column, row)] =
                {' ', terminal.foreground};
        }
    }
    terminal.cursor_column = 0;
    terminal.cursor_row = 0;
}

void terminal_put_char(Terminal& terminal, char character) {
    if (character == '\n') {
        terminal_newline(terminal);
        return;
    }
    if (character == '\r') {
        terminal.cursor_column = 0;
        return;
    }
    if (character == '\b') {
        if (terminal.cursor_column == 0) return;
        --terminal.cursor_column;
        TerminalCell& cell = g_terminal_cells[terminal_index(
            terminal, terminal.cursor_column, terminal.cursor_row)];
        cell = {' ', terminal.foreground};
        terminal_draw_cell(terminal, terminal.cursor_column, terminal.cursor_row);
        return;
    }
    if (character == '\t') {
        do {
            terminal_put_char(terminal, ' ');
        } while ((terminal.cursor_column % 4) != 0);
        return;
    }

    TerminalCell& cell = g_terminal_cells[terminal_index(
        terminal, terminal.cursor_column, terminal.cursor_row)];
    cell = {character, terminal.foreground};
    terminal_draw_cell(terminal, terminal.cursor_column, terminal.cursor_row);
    ++terminal.cursor_column;
    if (terminal.cursor_column >= terminal.columns) terminal_newline(terminal);
}

void terminal_write(Terminal& terminal, const char* text) {
    while (*text) terminal_put_char(terminal, *text++);
}

void terminal_write_unsigned(Terminal& terminal, uint64_t value) {
    char reverse[24];
    uint32_t count = 0;
    do {
        reverse[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) terminal_put_char(terminal, reverse[--count]);
}

Terminal terminal_initialize(const Framebuffer& framebuffer) {
    const uint32_t navy = pixel(framebuffer, 10, 18, 35);
    const uint32_t panel = pixel(framebuffer, 16, 27, 47);
    const uint32_t cyan = pixel(framebuffer, 57, 205, 219);
    const uint32_t white = pixel(framebuffer, 235, 242, 250);
    const uint32_t green = pixel(framebuffer, 72, 210, 140);
    const uint32_t muted = pixel(framebuffer, 145, 164, 190);

    fill_rect(framebuffer, 0, 0, framebuffer.width, framebuffer.height, navy);
    fill_rect(framebuffer, 0, 0, framebuffer.width, 8, cyan);
    draw_text(framebuffer, 24, 25, "ANOS 1.1 / INTERACTIVE KERNEL", cyan, 3);
    draw_text(framebuffer, 24, 59, "IRQ: ON / PS2: IRQ1 / PIT: 100 HZ", green, 2);
    fill_rect(framebuffer, 20, 88, framebuffer.width - 40,
              framebuffer.height - 132, panel);
    fill_rect(framebuffer, 20, 88, 6, framebuffer.height - 132, cyan);
    draw_text(framebuffer, 24, framebuffer.height - 31,
              "ANSOFTS / ANOS PROJECT / BUILD 1.1.0", muted, 2);

    Terminal terminal {};
    terminal.framebuffer = framebuffer;
    terminal.origin_x = 42;
    terminal.origin_y = 105;
    terminal.scale = 2;
    terminal.cell_width = 12;
    terminal.cell_height = 18;
    terminal.columns = (framebuffer.width - 84) / terminal.cell_width;
    terminal.rows = (framebuffer.height - 168) / terminal.cell_height;
    if (terminal.columns > MAX_TERMINAL_COLUMNS) terminal.columns = MAX_TERMINAL_COLUMNS;
    if (terminal.rows > MAX_TERMINAL_ROWS) terminal.rows = MAX_TERMINAL_ROWS;
    terminal.background = panel;
    terminal.foreground = white;
    terminal_clear(terminal);
    return terminal;
}

bool strings_equal(const char* left, const char* right) {
    while (*left != 0 && *right != 0) {
        if (*left++ != *right++) return false;
    }
    return *left == *right;
}

bool starts_with(const char* text, const char* prefix) {
    while (*prefix != 0) {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

const char* skip_spaces(const char* text) {
    while (*text == ' ') ++text;
    return text;
}

void cpu_vendor(char output[13]) {
    uint32_t eax = 0;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    asm volatile("cpuid"
                 : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    const uint32_t registers[3] = {ebx, edx, ecx};
    for (uint32_t register_index = 0; register_index < 3; ++register_index) {
        for (uint32_t byte = 0; byte < 4; ++byte) {
            output[register_index * 4 + byte] = static_cast<char>(
                registers[register_index] >> (byte * 8));
        }
    }
    output[12] = 0;
}

[[noreturn]] void reboot_system() {
    asm volatile("cli");
    if (ps2_wait_input_clear()) outb(0x64, 0xFE);
    halt_forever();
}

void execute_command(Terminal& terminal, const char* raw_command,
                     uint64_t usable_memory_bytes) {
    const char* command = skip_spaces(raw_command);

    if (*command == 0) return;
    if (strings_equal(command, "help")) {
        terminal_write(terminal,
            "COMANDI DISPONIBILI:\n"
            " help       elenco comandi\n"
            " about      informazioni sul progetto\n"
            " version    versione del kernel\n"
            " clear/cls  pulisce la console\n"
            " echo TESTO stampa un messaggio\n"
            " mem        memoria utilizzabile\n"
            " uptime     secondi dall'avvio\n"
            " ticks      interrupt timer ricevuti\n"
            " irq        statistiche interrupt\n"
            " cpu        produttore della CPU\n"
            " color NOME cambia colore testo\n"
            " reboot     riavvia la macchina\n"
            " halt       arresta la CPU\n");
    } else if (strings_equal(command, "about")) {
        terminal_write(terminal,
            "anOS e un sistema operativo x86-64 freestanding.\n"
            "Creato da Anthony Alessio Tralongo e anSofts.\n");
    } else if (strings_equal(command, "version")) {
        terminal_write(terminal, "anOS kernel 1.1.0 / x86-64 / UEFI\n");
    } else if (strings_equal(command, "clear") || strings_equal(command, "cls")) {
        terminal_clear(terminal);
    } else if (starts_with(command, "echo ")) {
        terminal_write(terminal, skip_spaces(command + 5));
        terminal_put_char(terminal, '\n');
    } else if (strings_equal(command, "echo")) {
        terminal_put_char(terminal, '\n');
    } else if (strings_equal(command, "mem")) {
        terminal_write(terminal, "Memoria utilizzabile: ");
        terminal_write_unsigned(terminal, usable_memory_bytes / (1024ULL * 1024ULL));
        terminal_write(terminal, " MiB\n");
    } else if (strings_equal(command, "uptime")) {
        terminal_write(terminal, "Uptime: ");
        terminal_write_unsigned(terminal, g_timer_ticks / PIT_FREQUENCY_HZ);
        terminal_write(terminal, " secondi\n");
    } else if (strings_equal(command, "ticks")) {
        terminal_write(terminal, "PIT ticks: ");
        terminal_write_unsigned(terminal, g_timer_ticks);
        terminal_put_char(terminal, '\n');
    } else if (strings_equal(command, "irq")) {
        terminal_write(terminal, "IRQ0 timer: ");
        terminal_write_unsigned(terminal, g_timer_ticks);
        terminal_write(terminal, " / IRQ1 tastiera: ");
        terminal_write_unsigned(terminal, g_keyboard_interrupts);
        terminal_put_char(terminal, '\n');
    } else if (strings_equal(command, "cpu")) {
        char vendor[13];
        cpu_vendor(vendor);
        terminal_write(terminal, "CPU vendor: ");
        terminal_write(terminal, vendor);
        terminal_put_char(terminal, '\n');
    } else if (starts_with(command, "color ")) {
        const char* name = skip_spaces(command + 6);
        if (strings_equal(name, "cyan")) {
            terminal.foreground = pixel(terminal.framebuffer, 57, 205, 219);
        } else if (strings_equal(name, "green")) {
            terminal.foreground = pixel(terminal.framebuffer, 72, 210, 140);
        } else if (strings_equal(name, "white")) {
            terminal.foreground = pixel(terminal.framebuffer, 235, 242, 250);
        } else if (strings_equal(name, "amber")) {
            terminal.foreground = pixel(terminal.framebuffer, 255, 191, 71);
        } else {
            terminal_write(terminal, "Colori: cyan, green, white, amber\n");
            return;
        }
        terminal_write(terminal, "Colore aggiornato.\n");
    } else if (strings_equal(command, "reboot")) {
        terminal_write(terminal, "Riavvio di anOS...\n");
        reboot_system();
    } else if (strings_equal(command, "halt")) {
        terminal_write(terminal, "CPU arrestata. Chiudi QEMU manualmente.\n");
        halt_forever();
    } else {
        terminal_write(terminal, "Comando sconosciuto: ");
        terminal_write(terminal, command);
        terminal_write(terminal, "\nDigita help per l'elenco.\n");
    }
}

uint64_t usable_memory(UINTN map_size, UINTN descriptor_size) {
    if (descriptor_size == 0) return 0;
    uint64_t pages = 0;
    for (UINTN offset = 0; offset + descriptor_size <= map_size;
         offset += descriptor_size) {
        const auto* descriptor = reinterpret_cast<const EFI_MEMORY_DESCRIPTOR*>(
            g_memory_map + offset);
        // RAM recuperabile dal kernel dopo ExitBootServices. MMIO e aree
        // runtime non vengono più conteggiate come nella 1.0.
        if (descriptor->Type == 1 || descriptor->Type == 2 ||
            descriptor->Type == 3 || descriptor->Type == 4 ||
            descriptor->Type == 7 || descriptor->Type == 9) {
            pages += descriptor->NumberOfPages;
        }
    }
    return pages * 4096ULL;
}

[[noreturn]] void shell_run(Terminal& terminal, uint64_t usable_memory_bytes) {
    terminal_write(terminal, "anOS 1.1 avviato. Interrupt e tastiera attivi.\n");
    terminal_write(terminal, "Digita help per vedere i comandi.\n\n");

    char command[128];
    for (;;) {
        terminal_write(terminal, "anOS> ");
        uint32_t length = 0;

        for (;;) {
            asm volatile("hlt");
            char character;
            while (key_buffer_pop(character)) {
                if (character == '\n') {
                    command[length] = 0;
                    terminal_put_char(terminal, '\n');
                    execute_command(terminal, command, usable_memory_bytes);
                    goto next_command;
                }
                if (character == '\b') {
                    if (length != 0) {
                        --length;
                        terminal_put_char(terminal, '\b');
                    }
                    continue;
                }
                if (character >= ' ' && character <= '~' && length < sizeof(command) - 1) {
                    command[length++] = character;
                    terminal_put_char(terminal, character);
                }
            }
        }

    next_command:
        continue;
    }
}

} // namespace

extern "C" void* memcpy(void* destination, const void* source, size_t count) {
    auto* out = static_cast<uint8_t*>(destination);
    const auto* in = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < count; ++i) out[i] = in[i];
    return destination;
}

extern "C" void* memset(void* destination, int value, size_t count) {
    auto* out = static_cast<uint8_t*>(destination);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<uint8_t>(value);
    return destination;
}

extern "C" void* memmove(void* destination, const void* source, size_t count) {
    auto* out = static_cast<uint8_t*>(destination);
    const auto* in = static_cast<const uint8_t*>(source);
    if (out < in) {
        for (size_t i = 0; i < count; ++i) out[i] = in[i];
    } else if (out > in) {
        for (size_t i = count; i != 0; --i) out[i - 1] = in[i - 1];
    }
    return destination;
}

extern "C" int memcmp(const void* left, const void* right, size_t count) {
    const auto* a = static_cast<const uint8_t*>(left);
    const auto* b = static_cast<const uint8_t*>(right);
    for (size_t i = 0; i < count; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

extern "C" uefi::EFI_STATUS EFIAPI efi_main(
    uefi::EFI_HANDLE image_handle,
    uefi::EFI_SYSTEM_TABLE* system_table) {
    using namespace uefi;

    serial_init();
    serial_write("anOS 1.1: ingresso UEFI\r\n");

    if (system_table == nullptr || system_table->BootServices == nullptr) {
        serial_write("anOS PANIC: EFI_SYSTEM_TABLE non valida\r\n");
        halt_forever();
    }

    EFI_BOOT_SERVICES* boot = system_table->BootServices;
    if (boot->SetWatchdogTimer != nullptr) {
        boot->SetWatchdogTimer(0, 0, 0, nullptr);
    }

    EFI_GRAPHICS_OUTPUT_PROTOCOL* graphics = nullptr;
    EFI_GUID graphics_guid = GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = boot->LocateProtocol(
        &graphics_guid, nullptr, reinterpret_cast<void**>(&graphics));
    if (is_error(status) || graphics == nullptr || graphics->Mode == nullptr ||
        graphics->Mode->Info == nullptr ||
        graphics->Mode->Info->PixelFormat == PixelBltOnly) {
        serial_write("anOS PANIC: framebuffer GOP non disponibile\r\n");
        halt_forever();
    }

    const Framebuffer framebuffer {
        reinterpret_cast<volatile uint32_t*>(graphics->Mode->FrameBufferBase),
        graphics->Mode->Info->HorizontalResolution,
        graphics->Mode->Info->VerticalResolution,
        graphics->Mode->Info->PixelsPerScanLine,
        graphics->Mode->Info->PixelFormat
    };

    UINTN map_size = sizeof(g_memory_map);
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    uint32_t descriptor_version = 0;

    for (uint32_t attempt = 0; attempt < 4; ++attempt) {
        map_size = sizeof(g_memory_map);
        status = boot->GetMemoryMap(
            &map_size,
            reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(g_memory_map),
            &map_key,
            &descriptor_size,
            &descriptor_version);
        if (is_error(status)) {
            serial_write("anOS PANIC: GetMemoryMap fallita\r\n");
            halt_forever();
        }

        status = boot->ExitBootServices(image_handle, map_key);
        if (status == SUCCESS) break;
        if (status != INVALID_PARAMETER) {
            serial_write("anOS PANIC: ExitBootServices fallita\r\n");
            halt_forever();
        }
    }

    if (status != SUCCESS) {
        serial_write("anOS PANIC: impossibile lasciare i servizi UEFI\r\n");
        halt_forever();
    }

    serial_write("anOS 1.1: ExitBootServices OK\r\n");
    Terminal terminal = terminal_initialize(framebuffer);
    const uint64_t memory_bytes = usable_memory(map_size, descriptor_size);

    interrupts_initialize();
    serial_write("anOS 1.1: IDT, PIC, PIT e PS/2 attivi\r\n");
    shell_run(terminal, memory_bytes);
}
