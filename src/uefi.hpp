#pragma once

#include <stdint.h>
#include <stddef.h>

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

namespace uefi {

using BOOLEAN = uint8_t;
using CHAR16 = uint16_t;
using UINTN = uint64_t;
using EFI_STATUS = uint64_t;
using EFI_HANDLE = void*;
using EFI_EVENT = void*;
using EFI_TPL = UINTN;
using EFI_PHYSICAL_ADDRESS = uint64_t;
using EFI_VIRTUAL_ADDRESS = uint64_t;

constexpr EFI_STATUS SUCCESS = 0;
constexpr EFI_STATUS ERROR_BIT = 0x8000000000000000ULL;
constexpr EFI_STATUS INVALID_PARAMETER = ERROR_BIT | 2;
constexpr EFI_STATUS BUFFER_TOO_SMALL = ERROR_BIT | 5;

inline bool is_error(EFI_STATUS status) {
    return (status & ERROR_BIT) != 0;
}

struct EFI_GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

struct EFI_TABLE_HEADER {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
};

struct EFI_INPUT_KEY {
    uint16_t ScanCode;
    CHAR16 UnicodeChar;
};

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_RUNTIME_SERVICES;
struct EFI_BOOT_SERVICES;
struct EFI_CONFIGURATION_TABLE;

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS(EFIAPI* Reset)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL*, BOOLEAN);
    EFI_STATUS(EFIAPI* ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL*, EFI_INPUT_KEY*);
    EFI_EVENT WaitForKey;
};

struct SIMPLE_TEXT_OUTPUT_MODE {
    int32_t MaxMode;
    int32_t Mode;
    int32_t Attribute;
    int32_t CursorColumn;
    int32_t CursorRow;
    BOOLEAN CursorVisible;
};

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS(EFIAPI* Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, BOOLEAN);
    EFI_STATUS(EFIAPI* OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, CHAR16*);
    void* TestString;
    void* QueryMode;
    void* SetMode;
    void* SetAttribute;
    void* ClearScreen;
    void* SetCursorPosition;
    void* EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE* Mode;
};

struct EFI_MEMORY_DESCRIPTOR {
    uint32_t Type;
    uint32_t Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

enum EFI_GRAPHICS_PIXEL_FORMAT : uint32_t {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
};

struct EFI_PIXEL_BITMASK {
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
};

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    uint32_t PixelsPerScanLine;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void* QueryMode;
    void* SetMode;
    void* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};

using GET_MEMORY_MAP = EFI_STATUS(EFIAPI*)(
    UINTN*, EFI_MEMORY_DESCRIPTOR*, UINTN*, UINTN*, uint32_t*);
using EXIT_BOOT_SERVICES = EFI_STATUS(EFIAPI*)(EFI_HANDLE, UINTN);
using LOCATE_PROTOCOL = EFI_STATUS(EFIAPI*)(EFI_GUID*, void*, void**);
using SET_WATCHDOG_TIMER = EFI_STATUS(EFIAPI*)(UINTN, uint64_t, UINTN, CHAR16*);

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void* RaiseTPL;
    void* RestoreTPL;
    void* AllocatePages;
    void* FreePages;
    GET_MEMORY_MAP GetMemoryMap;
    void* AllocatePool;
    void* FreePool;
    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    void* HandleProtocol;
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    EXIT_BOOT_SERVICES ExitBootServices;
    void* GetNextMonotonicCount;
    void* Stall;
    SET_WATCHDOG_TIMER SetWatchdogTimer;
    void* ConnectController;
    void* DisconnectController;
    void* OpenProtocol;
    void* CloseProtocol;
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    LOCATE_PROTOCOL LocateProtocol;
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;
    void* CalculateCrc32;
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
};

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16* FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    EFI_RUNTIME_SERVICES* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE* ConfigurationTable;
};

static_assert(sizeof(EFI_TABLE_HEADER) == 24);
static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) == 40);
static_assert(offsetof(EFI_SYSTEM_TABLE, BootServices) == 96);
static_assert(offsetof(EFI_BOOT_SERVICES, GetMemoryMap) == 56);
static_assert(offsetof(EFI_BOOT_SERVICES, ExitBootServices) == 232);
static_assert(offsetof(EFI_BOOT_SERVICES, SetWatchdogTimer) == 256);
static_assert(offsetof(EFI_BOOT_SERVICES, LocateProtocol) == 320);
static_assert(offsetof(EFI_GRAPHICS_OUTPUT_PROTOCOL, Mode) == 24);

inline constexpr EFI_GUID GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042a9de, 0x23dc, 0x4a38,
    {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

} // namespace uefi
