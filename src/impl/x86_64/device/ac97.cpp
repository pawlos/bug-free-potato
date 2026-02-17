#include "../../../intf/ac97.h"
#include "../../../intf/io.h"
#include "../../../intf/virtual.h"
#include "../../../intf/kernel.h"
#include "../../../intf/pci.h"

// Declared in pci.cpp - direct PCI config space access
extern pt::uint32_t pciConfigReadDWord(const pt::uint8_t bus, const pt::uint8_t slot,
                                       const pt::uint8_t func, const pt::uint8_t offset);
extern void pciConfigWriteDWord(const pt::uint8_t bus, const pt::uint8_t slot,
                                const pt::uint8_t func, const pt::uint8_t offset,
                                pt::uint32_t value);

// ---------------------------------------------------------------------------
// Static member storage
// ---------------------------------------------------------------------------
bool         AC97::initialized = false;
pt::uint16_t AC97::pci_bus     = 0;
pt::uint8_t  AC97::pci_dev     = 0;
pt::uint16_t AC97::nam_base    = 0;
pt::uint16_t AC97::nabm_base   = 0;
AC97_BDL_Entry* AC97::bdl      = nullptr;
pt::uint8_t* AC97::dma_buf[AC97_MAX_BDL_ENTRIES] = {};

// ---------------------------------------------------------------------------
// PCI config helpers
// ---------------------------------------------------------------------------
pt::uint32_t AC97::pci_read_dword(pt::uint8_t offset) {
    return pciConfigReadDWord(static_cast<pt::uint8_t>(pci_bus), pci_dev, 0, offset);
}

void AC97::pci_write_dword(pt::uint8_t offset, pt::uint32_t value) {
    pciConfigWriteDWord(static_cast<pt::uint8_t>(pci_bus), pci_dev, 0, offset, value);
}

// ---------------------------------------------------------------------------
// NAM (mixer) helpers
// ---------------------------------------------------------------------------
pt::uint16_t AC97::nam_read(pt::uint8_t reg) {
    return IO::inw(nam_base + reg);
}

void AC97::nam_write(pt::uint8_t reg, pt::uint16_t value) {
    IO::outw(nam_base + reg, value);
}

// ---------------------------------------------------------------------------
// NABM (bus master) helpers
// ---------------------------------------------------------------------------
pt::uint8_t AC97::nabm_read_byte(pt::uint8_t reg) {
    return IO::inb(nabm_base + reg);
}

void AC97::nabm_write_byte(pt::uint8_t reg, pt::uint8_t value) {
    IO::outb(nabm_base + reg, value);
}

pt::uint16_t AC97::nabm_read_word(pt::uint8_t reg) {
    return IO::inw(nabm_base + reg);
}

void AC97::nabm_write_word(pt::uint8_t reg, pt::uint16_t value) {
    IO::outw(nabm_base + reg, value);
}

pt::uint32_t AC97::nabm_read_dword(pt::uint8_t reg) {
    return IO::ind(nabm_base + reg);
}

void AC97::nabm_write_dword(pt::uint8_t reg, pt::uint32_t value) {
    IO::outd(nabm_base + reg, value);
}

// ---------------------------------------------------------------------------
// reset_channel: put the PCM-Out DMA channel into a clean stopped state
// ---------------------------------------------------------------------------
void AC97::reset_channel() {
    // Write RR (Reset Registers) bit
    nabm_write_byte(AC97_NABM_PCM_OUT_CTL, AC97_CTL_RR);

    // Wait for hardware to clear the bit (up to ~10 ms)
    for (int t = 0; t < 10000; t++) {
        if (!(nabm_read_byte(AC97_NABM_PCM_OUT_CTL) & AC97_CTL_RR))
            break;
        IO::io_wait();
    }

    // Clear any pending status bits (write-1-to-clear)
    nabm_write_word(AC97_NABM_PCM_OUT_STATUS, 0x001C);
}

// ---------------------------------------------------------------------------
// reset_and_wait: cold-reset AC link, wait for codec ready
// ---------------------------------------------------------------------------
bool AC97::reset_and_wait() {
    klog("[AC97] Cold reset...\n");

    // Assert cold reset (bit 1 of GLOB_CTL = 0 means reset)
    nabm_write_dword(AC97_NABM_GLOB_CTL, 0);

    // Hold reset for a short time
    for (int i = 0; i < 10000; i++) IO::io_wait();

    // Release reset (set bit 1)
    nabm_write_dword(AC97_NABM_GLOB_CTL, AC97_GLOB_CTL_COLD);

    // Pulse NAM reset so the codec initialises its registers
    nam_write(AC97_NAM_RESET, 0x0000);
    for (int i = 0; i < 1000; i++) IO::io_wait();

    // Wait for codec-ready (bit 8 of GLOB_STA), timeout ~100 ms
    for (int t = 0; t < 100000; t++) {
        pt::uint32_t sta = nabm_read_dword(AC97_NABM_GLOB_STA);
        if (sta & AC97_GLOB_STA_CODEC_RDY) {
            klog("[AC97] Codec ready, GLOB_STA=%x\n", sta);
            return true;
        }
        IO::io_wait();
    }

    // Try alternate check via NAM: if master volume responds it's alive
    pt::uint16_t master_vol = nam_read(AC97_NAM_MASTER_VOLUME);
    if (master_vol != 0xFFFF) {
        klog("[AC97] Codec alive via NAM (master_vol=%x)\n", master_vol);
        return true;
    }

    klog("[AC97] Codec ready timeout\n");
    return false;
}

// ---------------------------------------------------------------------------
// set_sample_rate: write rate to NAM (enables VRA first if needed)
// ---------------------------------------------------------------------------
void AC97::set_sample_rate(pt::uint32_t rate) {
    // Read extended audio ID to check VRA support (Variable Rate Audio)
    pt::uint16_t ext_id = nam_read(AC97_NAM_EXT_AUDIO_ID);
    klog("[AC97] Extended Audio ID: %x\n", ext_id);

    if (ext_id & 0x0001) {
        // VRA supported - enable it so we can set arbitrary rates
        pt::uint16_t ext_ctrl = nam_read(AC97_NAM_EXT_AUDIO_CTRL);
        nam_write(AC97_NAM_EXT_AUDIO_CTRL, ext_ctrl | 0x0001);
        klog("[AC97] VRA enabled\n");
    }

    nam_write(AC97_NAM_PCM_FRONT_RATE, static_cast<pt::uint16_t>(rate));

    for (int i = 0; i < 10000; i++) IO::io_wait();

    pt::uint16_t actual = nam_read(AC97_NAM_PCM_FRONT_RATE);
    klog("[AC97] Sample rate: requested %d Hz, got %d Hz\n", rate, actual);
}

// ---------------------------------------------------------------------------
// initialize
// ---------------------------------------------------------------------------
bool AC97::initialize() {
    klog("[AC97] Initializing...\n");

    // --- Find device on PCI bus ---
    bool found = false;
    pci_device* devices = pci::enumerate();
    pci_device* d = devices;
    while (d && d->vendor_id != 0xFFFF) {
        if (d->class_code == AC97_CLASS_CODE &&
            d->subclass_code == AC97_SUBCLASS_CODE) {
            pci_bus = d->bus;
            pci_dev = d->device;
            found = true;
            klog("[AC97] Found at bus=%d dev=%d (vendor=%x device=%x)\n",
                 d->bus, d->device, d->vendor_id, d->device_id);
            break;
        }
        d++;
    }
    VMM::Instance()->kfree(devices);

    if (!found) {
        klog("[AC97] No AC97 device found on PCI bus\n");
        return false;
    }

    // --- Read BARs ---
    pt::uint32_t bar0 = pci_read_dword(0x10);  // NAM  - must be I/O space
    pt::uint32_t bar1 = pci_read_dword(0x14);  // NABM - must be I/O space

    klog("[AC97] BAR0=%x BAR1=%x\n", bar0, bar1);

    if (!(bar0 & 0x1) || !(bar1 & 0x1)) {
        klog("[AC97] ERROR: BARs not I/O mapped (expected bit 0 set)\n");
        return false;
    }

    nam_base  = bar0 & 0xFFFC;
    nabm_base = bar1 & 0xFFFC;
    klog("[AC97] NAM base=%x NABM base=%x\n", nam_base, nabm_base);

    // --- Enable I/O space + Bus Master in PCI command register ---
    pt::uint32_t cmd_sts = pci_read_dword(0x04);
    pt::uint16_t cmd     = cmd_sts & 0xFFFF;
    if ((cmd & 0x05) != 0x05) {
        cmd |= 0x05;  // I/O space enable (bit 0) + bus master enable (bit 2)
        pci_write_dword(0x04, (cmd_sts & 0xFFFF0000) | cmd);
        klog("[AC97] Enabled I/O space + bus master (cmd=%x)\n", cmd);
    }

    // --- Reset AC link and wait for codec ---
    if (!reset_and_wait()) {
        klog("[AC97] Codec did not become ready\n");
        return false;
    }

    // --- Configure sample rate (48 kHz is AC97 default; many codecs fix at this) ---
    set_sample_rate(48000);

    // --- Unmute and set max volume ---
    set_volume(100);

    // --- Allocate BDL (32 entries × 8 bytes = 256 bytes) ---
    bdl = static_cast<AC97_BDL_Entry*>(
        VMM::Instance()->kcalloc(sizeof(AC97_BDL_Entry) * AC97_MAX_BDL_ENTRIES));
    if (!bdl) {
        klog("[AC97] BDL allocation failed\n");
        return false;
    }

    // --- Allocate DMA buffers ---
    for (int i = 0; i < AC97_MAX_BDL_ENTRIES; i++) {
        dma_buf[i] = static_cast<pt::uint8_t*>(
            VMM::Instance()->kcalloc(AC97_DMA_BUFFER_BYTES));
        if (!dma_buf[i]) {
            klog("[AC97] DMA buffer %d allocation failed\n", i);
            // Free previously allocated buffers
            for (int j = 0; j < i; j++) VMM::Instance()->kfree(dma_buf[j]);
            VMM::Instance()->kfree(bdl);
            bdl = nullptr;
            return false;
        }
    }

    initialized = true;
    klog("[AC97] Ready (%d x %d KB DMA buffers)\n",
         AC97_MAX_BDL_ENTRIES, AC97_DMA_BUFFER_BYTES / 1024);
    return true;
}

// ---------------------------------------------------------------------------
// is_present
// ---------------------------------------------------------------------------
bool AC97::is_present() { return initialized; }

// ---------------------------------------------------------------------------
// set_volume (0 = mute, 100 = max)
// ---------------------------------------------------------------------------
void AC97::set_volume(pt::uint8_t percent) {
    if (percent > 100) percent = 100;

    // AC97 master volume: 6-bit attenuation per channel (0 = max, 63 = min)
    // Map 0%->mute, 1-100%->attenuation 31..0
    pt::uint16_t vol;
    if (percent == 0) {
        vol = AC97_VOLUME_MUTE;
    } else {
        // attenuation = (100 - percent) * 31 / 100
        pt::uint8_t atten = static_cast<pt::uint8_t>((100u - percent) * 31u / 100u);
        vol = (atten << 8) | atten;  // left = right
    }

    nam_write(AC97_NAM_MASTER_VOLUME,    vol);
    nam_write(AC97_NAM_HEADPHONE_VOLUME, vol);
    nam_write(AC97_NAM_PCM_OUT_VOLUME,   AC97_VOLUME_MAX);  // PCM at full scale
    nam_write(AC97_NAM_MIC_VOLUME,       AC97_VOLUME_MUTE); // mute mic

    klog("[AC97] Volume %d%% (reg=%x)\n", percent, vol);
}

// ---------------------------------------------------------------------------
// start_dma: fill BDL and kick off the PCM-Out DMA engine
// ---------------------------------------------------------------------------
bool AC97::start_dma(const pt::uint8_t* data, pt::uint32_t length) {
    // Reset the channel first
    reset_channel();

    // Fill BDL entries from contiguous data
    pt::uint32_t remaining = length;
    pt::uint8_t  n_entries = 0;

    while (remaining > 0 && n_entries < AC97_MAX_BDL_ENTRIES) {
        pt::uint32_t chunk = remaining < AC97_DMA_BUFFER_BYTES
                           ? remaining : AC97_DMA_BUFFER_BYTES;

        // Copy chunk into DMA buffer
        const pt::uint8_t* src = data + (pt::uint32_t)n_entries * AC97_DMA_BUFFER_BYTES;
        pt::uint8_t* dst = dma_buf[n_entries];
        for (pt::uint32_t b = 0; b < chunk; b++) dst[b] = src[b];

        // Physical address (identity-mapped kernel)
        pt::uint32_t phys = static_cast<pt::uint32_t>(
            VMM::virt_to_phys(dma_buf[n_entries]));

        // Samples: 1 sample = 2 bytes (16-bit); stereo means 4 bytes per frame.
        // AC97 sample_count field = total 16-bit words in the buffer.
        pt::uint16_t num_samples = static_cast<pt::uint16_t>(chunk / 2);

        bdl[n_entries].phys_addr   = phys;
        bdl[n_entries].num_samples = num_samples;
        bdl[n_entries].flags       = 0;

        remaining -= chunk;
        n_entries++;
    }

    // Mark the last entry
    bdl[n_entries - 1].flags = AC97_BDL_FLAG_LAST | AC97_BDL_FLAG_IOC;

    // Memory fence so the CPU writes reach RAM before the DMA controller reads them
    asm volatile("mfence" ::: "memory");

    // Program BDL base address
    pt::uint32_t bdl_phys = static_cast<pt::uint32_t>(VMM::virt_to_phys(bdl));
    nabm_write_dword(AC97_NABM_PCM_OUT_BDL_BASE, bdl_phys);

    // Set Last Valid Index (0-based)
    nabm_write_byte(AC97_NABM_PCM_OUT_LVI, n_entries - 1);

    // Start DMA: RPBM = 1
    nabm_write_byte(AC97_NABM_PCM_OUT_CTL, AC97_CTL_RPBM);

    klog("[AC97] DMA started: %d entries, %d bytes, BDL@%x\n",
         n_entries, length, bdl_phys);
    return true;
}

// ---------------------------------------------------------------------------
// play_pcm
// ---------------------------------------------------------------------------
bool AC97::play_pcm(const pt::uint8_t* data, pt::uint32_t length, pt::uint32_t rate) {
    if (!initialized) { klog("[AC97] Not initialized\n"); return false; }
    if (!data || length == 0) { klog("[AC97] Invalid data\n"); return false; }

    stop();
    set_sample_rate(rate);
    return start_dma(data, length);
}

// ---------------------------------------------------------------------------
// play_beep: generate square wave in DMA buffer 0 and play it
// ---------------------------------------------------------------------------
bool AC97::play_beep(pt::uint32_t frequency_hz, pt::uint32_t duration_ms) {
    if (!initialized) { klog("[AC97] Not initialized\n"); return false; }

    // Use a fixed 48 kHz sample rate
    constexpr pt::uint32_t RATE = 48000;

    // Number of frames (stereo) we can fit in one DMA buffer
    // One frame = 4 bytes (2 × 16-bit samples)
    constexpr pt::uint32_t MAX_FRAMES = AC97_DMA_BUFFER_BYTES / 4;

    // Requested frames
    pt::uint32_t total_frames = (RATE * duration_ms) / 1000;
    if (total_frames > MAX_FRAMES) total_frames = MAX_FRAMES;

    // Frames per half-period of the square wave
    pt::uint32_t half_period = RATE / (frequency_hz * 2);
    if (half_period == 0) half_period = 1;

    constexpr pt::int16_t AMPLITUDE = 16000;  // ~50% of full scale

    pt::uint8_t* buf  = dma_buf[0];
    pt::uint32_t phase = 0;

    for (pt::uint32_t f = 0; f < total_frames; f++) {
        pt::int16_t s = (phase < half_period) ? AMPLITUDE : -AMPLITUDE;
        phase++;
        if (phase >= half_period * 2) phase = 0;

        // Left channel (little-endian 16-bit)
        buf[f * 4 + 0] = static_cast<pt::uint8_t>(s & 0xFF);
        buf[f * 4 + 1] = static_cast<pt::uint8_t>((s >> 8) & 0xFF);
        // Right channel (same)
        buf[f * 4 + 2] = buf[f * 4 + 0];
        buf[f * 4 + 3] = buf[f * 4 + 1];
    }

    pt::uint32_t byte_count = total_frames * 4;
    klog("[AC97] play_beep: %d Hz, %d ms, %d bytes\n",
         frequency_hz, duration_ms, byte_count);

    stop();
    set_sample_rate(RATE);

    // Setup single BDL entry directly (already in dma_buf[0])
    reset_channel();

    pt::uint32_t phys = static_cast<pt::uint32_t>(VMM::virt_to_phys(dma_buf[0]));

    bdl[0].phys_addr   = phys;
    bdl[0].num_samples = static_cast<pt::uint16_t>(byte_count / 2);
    bdl[0].flags       = AC97_BDL_FLAG_LAST | AC97_BDL_FLAG_IOC;

    asm volatile("mfence" ::: "memory");

    pt::uint32_t bdl_phys = static_cast<pt::uint32_t>(VMM::virt_to_phys(bdl));
    nabm_write_dword(AC97_NABM_PCM_OUT_BDL_BASE, bdl_phys);
    nabm_write_byte(AC97_NABM_PCM_OUT_LVI, 0);
    nabm_write_byte(AC97_NABM_PCM_OUT_CTL, AC97_CTL_RPBM);

    klog("[AC97] Beep DMA started (BDL@%x, samples=%d)\n",
         bdl_phys, bdl[0].num_samples);
    return true;
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------
void AC97::stop() {
    if (!initialized) return;
    // Clear the RUN bit
    nabm_write_byte(AC97_NABM_PCM_OUT_CTL, 0);
    // Wait up to ~1 ms for DMA to halt
    for (int t = 0; t < 1000; t++) {
        if (nabm_read_word(AC97_NABM_PCM_OUT_STATUS) & AC97_STS_DCH)
            break;
        IO::io_wait();
    }
}

// ---------------------------------------------------------------------------
// is_playing
// ---------------------------------------------------------------------------
bool AC97::is_playing() {
    if (!initialized) return false;
    // DMA Controller Halted bit is 0 while playing
    return !(nabm_read_word(AC97_NABM_PCM_OUT_STATUS) & AC97_STS_DCH);
}
