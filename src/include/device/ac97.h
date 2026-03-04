#pragma once

#include "defs.h"
#include "pci.h"

// AC97 (Audio Codec '97) driver
// Supports Intel ICH-style AC97 controllers emulated by QEMU
//
// Architecture:
//   NAM  (Native Audio Mixer)      - accessed via BAR0 I/O ports; controls codec
//   NABM (Native Audio Bus Master) - accessed via BAR1 I/O ports; controls DMA
//
// QEMU invocation:
//   -audiodev <backend>,id=audio0 -device AC97,audiodev=audio0
//
// PCI identification: class=0x04 (Multimedia), subclass=0x01 (Audio)

// PCI class/subclass
constexpr pt::uint8_t AC97_CLASS_CODE    = 0x04;
constexpr pt::uint8_t AC97_SUBCLASS_CODE = 0x01;

// NAM register offsets (from BAR0)
constexpr pt::uint8_t AC97_NAM_RESET           = 0x00;  // Codec reset (write any value)
constexpr pt::uint8_t AC97_NAM_MASTER_VOLUME   = 0x02;  // Master output volume
constexpr pt::uint8_t AC97_NAM_HEADPHONE_VOLUME = 0x04; // Headphone volume
constexpr pt::uint8_t AC97_NAM_PCM_OUT_VOLUME  = 0x18;  // PCM-out volume
constexpr pt::uint8_t AC97_NAM_MIC_VOLUME      = 0x0E;  // Microphone volume
constexpr pt::uint8_t AC97_NAM_EXT_AUDIO_ID    = 0x28;  // Extended audio ID
constexpr pt::uint8_t AC97_NAM_EXT_AUDIO_CTRL  = 0x2A;  // Extended audio control
constexpr pt::uint8_t AC97_NAM_PCM_FRONT_RATE  = 0x2C;  // PCM front DAC sample rate

// NABM register offsets (from BAR1)
// PCM-Out channel (channel 1, base offset 0x10)
constexpr pt::uint8_t AC97_NABM_PCM_OUT_BDL_BASE = 0x10;  // BDL physical base address (dword)
constexpr pt::uint8_t AC97_NABM_PCM_OUT_CIV      = 0x14;  // Current Index Value (byte)
constexpr pt::uint8_t AC97_NABM_PCM_OUT_LVI      = 0x15;  // Last Valid Index (byte)
constexpr pt::uint8_t AC97_NABM_PCM_OUT_STATUS   = 0x16;  // Status register (word)
constexpr pt::uint8_t AC97_NABM_PCM_OUT_SAMPLES  = 0x18;  // Samples remaining (word)
constexpr pt::uint8_t AC97_NABM_PCM_OUT_CTL      = 0x1B;  // Control register (byte)

// Global NABM registers
constexpr pt::uint8_t AC97_NABM_GLOB_CTL = 0x2C;  // Global control (dword)
constexpr pt::uint8_t AC97_NABM_GLOB_STA = 0x30;  // Global status  (dword)

// Control register bits (AC97_NABM_PCM_OUT_CTL)
constexpr pt::uint8_t AC97_CTL_RPBM  = 0x01;  // Run/Pause Bus Master (DMA run)
constexpr pt::uint8_t AC97_CTL_RR    = 0x02;  // Reset Registers (channel reset)
constexpr pt::uint8_t AC97_CTL_LVBIE = 0x04;  // Last Valid Buffer Interrupt Enable
constexpr pt::uint8_t AC97_CTL_FEIE  = 0x08;  // FIFO Error Interrupt Enable
constexpr pt::uint8_t AC97_CTL_IOCE  = 0x10;  // Interrupt On Completion Enable

// Status register bits (AC97_NABM_PCM_OUT_STATUS)
constexpr pt::uint16_t AC97_STS_DCH   = 0x01;  // DMA Controller Halted
constexpr pt::uint16_t AC97_STS_CELV  = 0x02;  // Current Equals Last Valid
constexpr pt::uint16_t AC97_STS_LVBCI = 0x04;  // Last Valid Buffer Completion Interrupt
constexpr pt::uint16_t AC97_STS_BCIS  = 0x08;  // Buffer Completion Interrupt Status
constexpr pt::uint16_t AC97_STS_FIFOE = 0x10;  // FIFO Error

// Global control bits
constexpr pt::uint32_t AC97_GLOB_CTL_GIE  = 0x00000001;  // GPI Interrupt Enable
constexpr pt::uint32_t AC97_GLOB_CTL_COLD = 0x00000002;  // Cold Reset (0 = reset, 1 = run)

// Global status bits
constexpr pt::uint32_t AC97_GLOB_STA_CODEC_RDY = 0x00000100;  // Codec 0 ready

// Volume: bit 15 = mute, bits 8-13 = left attenuation, bits 0-5 = right attenuation
// 0x0000 = max volume (0 dB), 0x1F1F = min volume (-46.5 dB)
constexpr pt::uint16_t AC97_VOLUME_MAX  = 0x0000;
constexpr pt::uint16_t AC97_VOLUME_MUTE = 0x8000;

// Buffer Descriptor List entry (8 bytes, must be 8-byte aligned)
struct __attribute__((packed)) AC97_BDL_Entry {
    pt::uint32_t phys_addr;    // Physical address of the audio data buffer
    pt::uint16_t num_samples;  // Number of 16-bit samples (not bytes)
    pt::uint16_t flags;        // Bit 15: last entry, Bit 14: interrupt on completion
};

// BDL flags
constexpr pt::uint16_t AC97_BDL_FLAG_LAST = 0x8000;  // Last buffer in list
constexpr pt::uint16_t AC97_BDL_FLAG_IOC  = 0x4000;  // Interrupt on completion

// AC97 hardware limits
constexpr pt::uint8_t  AC97_MAX_BDL_ENTRIES  = 32;
constexpr pt::uint32_t AC97_DMA_BUFFER_BYTES = 0x8000;  // 32 KB per DMA buffer

class AC97 {
public:
    // Find AC97 on PCI bus, reset codec, configure volume.
    // Returns true on success.
    static bool initialize();

    // True if initialize() succeeded.
    static bool is_present();

    // Generate a square-wave beep internally and play it.
    // Frequency Hz, duration in milliseconds (capped by one DMA buffer ~340 ms at 48 kHz).
    static bool play_beep(pt::uint32_t frequency_hz = 880, pt::uint32_t duration_ms = 500);

    // Play raw 16-bit signed little-endian stereo PCM at the given sample rate.
    // data    : pointer to PCM samples
    // length  : byte count
    // rate    : sample rate in Hz (default 48000)
    static bool play_pcm(const pt::uint8_t* data, pt::uint32_t length,
                         pt::uint32_t rate = 48000);

    // Stop DMA playback.
    static void stop();

    // True while DMA is running.
    static bool is_playing();

    // Set master volume 0 (mute) .. 100 (max).
    static void set_volume(pt::uint8_t percent);

private:
    static bool         initialized;
    static pt::uint16_t pci_bus;
    static pt::uint8_t  pci_dev;
    static pt::uint16_t nam_base;   // BAR0 I/O base for NAM
    static pt::uint16_t nabm_base;  // BAR1 I/O base for NABM

    // Allocated BDL (32 × 8 bytes)
    static AC97_BDL_Entry* bdl;
    // Allocated DMA buffers (one per BDL entry)
    static pt::uint8_t* dma_buf[AC97_MAX_BDL_ENTRIES];

    // PCI config access (uses global pciConfigReadDWord / pciConfigWriteDWord)
    static pt::uint32_t pci_read_dword(pt::uint8_t offset);
    static void         pci_write_dword(pt::uint8_t offset, pt::uint32_t value);

    // NAM (mixer) register access
    static pt::uint16_t nam_read(pt::uint8_t reg);
    static void         nam_write(pt::uint8_t reg, pt::uint16_t value);

    // NABM (bus master) register access
    static pt::uint8_t  nabm_read_byte(pt::uint8_t reg);
    static void         nabm_write_byte(pt::uint8_t reg, pt::uint8_t value);
    static pt::uint16_t nabm_read_word(pt::uint8_t reg);
    static void         nabm_write_word(pt::uint8_t reg, pt::uint16_t value);
    static pt::uint32_t nabm_read_dword(pt::uint8_t reg);
    static void         nabm_write_dword(pt::uint8_t reg, pt::uint32_t value);

    // Reset AC link and wait for codec ready
    static bool reset_and_wait();

    // Set sample rate on NAM (enables VRA if codec supports it)
    static void set_sample_rate(pt::uint32_t rate);

    // Fill the BDL for a contiguous audio buffer, start DMA
    static bool start_dma(const pt::uint8_t* data, pt::uint32_t length);

    // Reset PCM-Out channel registers
    static void reset_channel();
};
