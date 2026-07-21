#include "uefi.hpp"
#include "font.hpp"

// Mantiene una relocation PE/COFF nel binario. Il firmware UEFI può così
// caricare anOS a qualunque indirizzo fisico disponibile, non solo alla base
// preferita scelta dal linker.
extern "C" __attribute__((used)) void* g_anos_relocation_anchor =
    reinterpret_cast<void*>(&g_anos_relocation_anchor);

namespace {

using namespace uefi;

struct Framebuffer {
    volatile uint32_t* base;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    EFI_GRAPHICS_PIXEL_FORMAT format;
};

alignas(16) uint8_t g_memory_map[128 * 1024];

inline void outb(uint16_t port, uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline uint8_t inb(uint16_t port) {
    uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
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

void append_decimal(char* output, uint64_t value) {
    char reverse[24];
    uint32_t count = 0;
    do {
        reverse[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);

    uint32_t end = 0;
    while (output[end] != '\0') ++end;
    while (count != 0) output[end++] = reverse[--count];
    output[end] = '\0';
}

uint64_t installed_memory_bytes(UINTN map_size, UINTN descriptor_size) {
    if (descriptor_size == 0) return 0;
    uint64_t pages = 0;
    for (UINTN offset = 0; offset + descriptor_size <= map_size;
         offset += descriptor_size) {
        const auto* descriptor = reinterpret_cast<const EFI_MEMORY_DESCRIPTOR*>(
            g_memory_map + offset);
        pages += descriptor->NumberOfPages;
    }
    return pages * 4096ULL;
}

[[noreturn]] void halt_forever() {
    asm volatile("cli");
    for (;;) asm volatile("hlt");
}

void render_desktop(const Framebuffer& fb, uint64_t memory_bytes) {
    const uint32_t navy = pixel(fb, 10, 18, 35);
    const uint32_t panel = pixel(fb, 20, 32, 55);
    const uint32_t cyan = pixel(fb, 57, 205, 219);
    const uint32_t white = pixel(fb, 235, 242, 250);
    const uint32_t muted = pixel(fb, 145, 164, 190);
    const uint32_t green = pixel(fb, 72, 210, 140);

    fill_rect(fb, 0, 0, fb.width, fb.height, navy);
    fill_rect(fb, 0, 0, fb.width, 8, cyan);

    const uint32_t margin = fb.width >= 800 ? 70 : 24;
    const uint32_t panel_width = fb.width > margin * 2 ? fb.width - margin * 2 : fb.width;
    fill_rect(fb, margin, 70, panel_width, 340, panel);
    fill_rect(fb, margin, 70, 8, 340, cyan);

    draw_text(fb, margin + 38, 105, "ANOS 1.0", cyan, 5);
    draw_text(fb, margin + 40, 170, "KERNEL X86-64 ATTIVO", white, 3);
    draw_text(fb, margin + 40, 215, "UEFI BOOT SERVICES: DISATTIVATI", green, 2);
    draw_text(fb, margin + 40, 250, "FRAMEBUFFER: ACCESSO DIRETTO", green, 2);

    char memory_line[] = "MEMORIA RILEVATA:                         ";
    // Manteniamo la stringa senza libreria standard e inseriamo il valore in coda.
    memory_line[17] = '\0';
    append_decimal(memory_line, memory_bytes / (1024ULL * 1024ULL));
    uint32_t length = 0;
    while (memory_line[length] != '\0') ++length;
    memory_line[length++] = ' ';
    memory_line[length++] = 'M';
    memory_line[length++] = 'I';
    memory_line[length++] = 'B';
    memory_line[length] = '\0';
    draw_text(fb, margin + 40, 285, memory_line, muted, 2);

    draw_text(fb, margin + 40, 350, "> PRIMO AVVIO COMPLETATO", white, 2);
    draw_text(fb, margin, fb.height > 55 ? fb.height - 45 : 0,
              "ANSOFTS / ANOS PROJECT / BUILD 1.0.0", muted, 2);
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
    serial_write("anOS 1.0: ingresso UEFI\r\n");

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

    Framebuffer framebuffer {
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

    // La mappa può cambiare tra GetMemoryMap ed ExitBootServices. Riproviamo
    // acquisendo ogni volta una chiave aggiornata, come richiesto da UEFI.
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

    const uint64_t memory_bytes = installed_memory_bytes(map_size, descriptor_size);
    serial_write("anOS 1.0: ExitBootServices OK, kernel autonomo\r\n");
    render_desktop(framebuffer, memory_bytes);
    serial_write("anOS 1.0: framebuffer disegnato, arresto CPU\r\n");
    halt_forever();
}
