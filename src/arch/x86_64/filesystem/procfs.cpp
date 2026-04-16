#include "fs/procfs.h"
#include "fs/fat32.h"
#include "task.h"
#include "virtual.h"
#include "vterm.h"
#include "device/timer.h"
#include "device/disk.h"
#include "kernel.h"

// ── Minimal buffer-builder (no snprintf in kernel) ───────────────────────────

static int pb_str(char* buf, int pos, int cap, const char* s) {
    while (*s && pos < cap - 1) buf[pos++] = *s++;
    return pos;
}

static int pb_uint(char* buf, int pos, int cap, pt::uint64_t v) {
    if (pos >= cap - 1) return pos;
    if (v == 0) { buf[pos++] = '0'; return pos; }
    char tmp[21]; int n = 0;
    while (v) { tmp[n++] = '0' + (int)(v % 10); v /= 10; }
    for (int i = n - 1; i >= 0 && pos < cap - 1; i--) buf[pos++] = tmp[i];
    return pos;
}

static int pb_hex(char* buf, int pos, int cap, pt::uint64_t v) {
    const char* hex = "0123456789abcdef";
    pos = pb_str(buf, pos, cap, "0x");
    for (int shift = 60; shift >= 0 && pos < cap - 1; shift -= 4)
        buf[pos++] = hex[(v >> shift) & 0xF];
    return pos;
}

static int pb_nl(char* buf, int pos, int cap) {
    if (pos < cap - 1) buf[pos++] = '\n';
    return pos;
}

// ── mount ────────────────────────────────────────────────────────────────────

bool ProcFS::mount() {
    // Read FAT32 boot sector to extract volume label.
    // FAT32_BPB_EXT starts at byte 36; volume_label is at offset 19 within
    // EXT = byte 71 in the boot sector.
    static pt::uint8_t sector[512] __attribute__((aligned(4)));
    if (!Disk::read_sector(0, sector)) {
        volume_label[0] = '?'; volume_label[1] = '\0';
        return true;
    }
    const pt::uint8_t* lbl = sector + 71;
    int i = 0;
    for (; i < 11 && lbl[i] != ' ' && lbl[i] != '\0'; i++)
        volume_label[i] = (char)lbl[i];
    volume_label[i] = '\0';
    klog("[ProcFS] mounted, volume label: %s\n", volume_label);
    return true;
}

// ── Content generators ───────────────────────────────────────────────────────

int ProcFS::gen_version(char* buf, int cap) {
    int p = 0;
    p = pb_str(buf, p, cap, volume_label);
    p = pb_nl(buf, p, cap);
    return p;
}

int ProcFS::gen_meminfo(char* buf, int cap) {
    pt::size_t total = vmm.get_total_mem();
    pt::size_t free  = vmm.get_free_mem();
    int p = 0;
    p = pb_str(buf, p, cap, "MemTotal: ");
    p = pb_uint(buf, p, cap, total / 1024);
    p = pb_str(buf, p, cap, " kB\n");
    p = pb_str(buf, p, cap, "MemFree:  ");
    p = pb_uint(buf, p, cap, free / 1024);
    p = pb_str(buf, p, cap, " kB\n");
    return p;
}

int ProcFS::gen_uptime(char* buf, int cap) {
    pt::uint64_t us   = get_microseconds();
    pt::uint64_t secs = us / 1000000;
    pt::uint64_t frac = (us % 1000000) / 1000;  // milliseconds
    int p = 0;
    p = pb_uint(buf, p, cap, secs);
    if (p < cap - 1) buf[p++] = '.';
    if (frac < 100 && p < cap - 1) buf[p++] = '0';
    if (frac < 10  && p < cap - 1) buf[p++] = '0';
    p = pb_uint(buf, p, cap, frac);
    p = pb_nl(buf, p, cap);
    return p;
}

// State code: TASK_READY=0, TASK_RUNNING=1, TASK_BLOCKED=2, TASK_DEAD=3, TASK_ZOMBIE=4
static const char state_char[] = { 'R', 'R', 'B', 'D', 'Z' };

int ProcFS::gen_status(pt::uint32_t pid, char* buf, int cap) {
    Task* t = TaskScheduler::get_task(pid);
    if (!t || t->state == TASK_DEAD) return 0;
    int p = 0;
    p = pb_str(buf, p, cap, "Name:  ");
    p = pb_str(buf, p, cap, t->name);
    p = pb_nl(buf, p, cap);
    p = pb_str(buf, p, cap, "Pid:   ");
    p = pb_uint(buf, p, cap, pid);
    p = pb_nl(buf, p, cap);
    p = pb_str(buf, p, cap, "PPid:  ");
    p = pb_uint(buf, p, cap, t->parent_id == 0xFFFFFFFF ? 0 : t->parent_id);
    p = pb_nl(buf, p, cap);
    p = pb_str(buf, p, cap, "State: ");
    int si = (int)t->state;
    if (p < cap - 1) buf[p++] = (si >= 0 && si < 5) ? state_char[si] : '?';
    p = pb_nl(buf, p, cap);
    p = pb_str(buf, p, cap, "Ticks: ");
    p = pb_uint(buf, p, cap, t->ticks_alive);
    p = pb_nl(buf, p, cap);
    p = pb_str(buf, p, cap, "User:  ");
    p = pb_str(buf, p, cap, t->user_mode ? "yes" : "no");
    p = pb_nl(buf, p, cap);
    return p;
}

int ProcFS::gen_maps(pt::uint32_t pid, char* buf, int cap) {
    Task* t = TaskScheduler::get_task(pid);
    if (!t || t->state == TASK_DEAD) return 0;
    int p = 0;
    if (t->user_mode) {
        p = pb_str(buf, p, cap, "code:      ");
        p = pb_hex(buf, p, cap, TaskScheduler::USER_CODE_BASE);
        p = pb_nl(buf, p, cap);
        p = pb_str(buf, p, cap, "heap_top:  ");
        p = pb_hex(buf, p, cap, t->user_heap_top);
        p = pb_nl(buf, p, cap);
        p = pb_str(buf, p, cap, "stack_top: ");
        p = pb_hex(buf, p, cap, TaskScheduler::USER_STACK_TOP);
        p = pb_nl(buf, p, cap);
    } else {
        p = pb_str(buf, p, cap, "kernel task (no user address space)\n");
    }
    return p;
}

int ProcFS::gen_syscalls(pt::uint32_t pid, char* buf, int cap) {
    int p = 0;
    if (!g_perf_data) {
        p = pb_str(buf, p, cap, "(perf not active; run 'perf record' first)\n");
        return p;
    }
    if (pid >= TaskScheduler::MAX_TASKS) return 0;
    SyscallPerfData& pd = g_perf_data[pid];
    for (pt::size_t i = 0; i < NUM_SYSCALLS; i++) {
        if (pd.counts[i] == 0) continue;
        p = pb_str(buf, p, cap, g_syscall_names[i]);
        p = pb_str(buf, p, cap, ": ");
        p = pb_uint(buf, p, cap, pd.counts[i]);
        p = pb_str(buf, p, cap, " calls, ");
        p = pb_uint(buf, p, cap, pd.usec[i]);
        p = pb_str(buf, p, cap, " us\n");
    }
    if (p == 0)
        p = pb_str(buf, p, cap, "(no syscalls recorded for this task)\n");
    return p;
}

// ── Path parsing ──────────────────────────────────────────────────────────────

const char* ProcFS::parse_pid_path(const char* path, pt::uint32_t* pid_out) {
    pt::uint32_t pid = 0;
    const char* p = path;
    if (*p < '0' || *p > '9') return nullptr;
    while (*p >= '0' && *p <= '9') pid = pid * 10 + (*p++ - '0');
    if (*p != '/') return nullptr;
    *pid_out = pid;
    return p + 1;  // skip '/'
}

// ── install_content / open_file ──────────────────────────────────────────────

bool ProcFS::install_content(File* file, const char* buf, int len) {
    if (len <= 0) return false;
    char* heap = (char*)vmm.kmalloc((pt::size_t)(len + 1));
    if (!heap) return false;
    for (int i = 0; i < len; i++) heap[i] = buf[i];
    heap[len] = '\0';

    // Store ptr and size in fs_data[0..11] via byte-wise writes (no PLT memcpy).
    pt::uint64_t ptr = (pt::uint64_t)(pt::uintptr_t)heap;
    pt::uint32_t sz  = (pt::uint32_t)len;
    pt::uint8_t* d = file->fs_data;
    for (int i = 0; i < 8; i++) d[i]   = (pt::uint8_t)(ptr >> (i * 8));
    for (int i = 0; i < 4; i++) d[8+i] = (pt::uint8_t)(sz  >> (i * 8));

    file->file_size        = sz;
    file->current_position = 0;
    file->open             = true;
    file->type             = FdType::PROC_FILE;
    return true;
}

bool ProcFS::open_file(const char* path, File* file) {
    // path arrives already stripped of leading "proc/" by VFS.
    // Possible values: "version", "meminfo", "uptime",
    //                  "<pid>/status", "<pid>/maps", "<pid>/syscalls"

    // Set filename to the proc path (truncated to fit the 13-byte field).
    int fi = 0;
    while (path[fi] && fi < 12) { file->filename[fi] = path[fi]; fi++; }
    file->filename[fi] = '\0';

    constexpr int CAP = 4096;
    char buf[CAP];
    int len = 0;

    auto eq = [](const char* a, const char* b) {
        while (*a && *b) { if (*a++ != *b++) return false; }
        return *a == *b;
    };

    if (eq(path, "version")) {
        len = gen_version(buf, CAP);
    } else if (eq(path, "meminfo")) {
        len = gen_meminfo(buf, CAP);
    } else if (eq(path, "uptime")) {
        len = gen_uptime(buf, CAP);
    } else {
        pt::uint32_t pid = 0;
        const char* leaf = parse_pid_path(path, &pid);
        if (!leaf) return false;
        Task* t = TaskScheduler::get_task(pid);
        if (!t || t->state == TASK_DEAD) return false;

        if (eq(leaf, "status"))
            len = gen_status(pid, buf, CAP);
        else if (eq(leaf, "maps"))
            len = gen_maps(pid, buf, CAP);
        else if (eq(leaf, "syscalls"))
            len = gen_syscalls(pid, buf, CAP);
        else
            return false;
    }

    return install_content(file, buf, len);
}

// ── read / seek / close ───────────────────────────────────────────────────────

static char* proc_buf(File* file) {
    // Reconstruct 64-bit pointer from fs_data[0..7] (byte-wise, no PLT memcpy).
    const pt::uint8_t* s = file->fs_data;
    pt::uint64_t ptr = 0;
    for (int i = 0; i < 8; i++) ptr |= ((pt::uint64_t)s[i]) << (i * 8);
    return (char*)(pt::uintptr_t)ptr;
}

pt::uint32_t ProcFS::read_file(File* file, void* dst, pt::uint32_t n) {
    char* buf = proc_buf(file);
    if (!buf) return 0;
    pt::uint32_t remaining = file->file_size - file->current_position;
    if (n > remaining) n = remaining;
    const char* src = buf + file->current_position;
    char* out = (char*)dst;
    for (pt::uint32_t i = 0; i < n; i++) out[i] = src[i];
    file->current_position += n;
    return n;
}

pt::uint32_t ProcFS::seek_file(File* file, pt::int32_t offset, int whence) {
    pt::int32_t pos;
    if      (whence == 0) pos = offset;
    else if (whence == 1) pos = (pt::int32_t)file->current_position + offset;
    else                  pos = (pt::int32_t)file->file_size + offset;
    if (pos < 0) pos = 0;
    if ((pt::uint32_t)pos > file->file_size) pos = (pt::int32_t)file->file_size;
    file->current_position = (pt::uint32_t)pos;
    return (pt::uint32_t)pos;
}

void ProcFS::close_file(File* file) {
    char* buf = proc_buf(file);
    if (buf) vmm.kfree(buf);
    // Zero the stored pointer and size so it can't be double-freed.
    for (int i = 0; i < 12; i++) file->fs_data[i] = 0;
    file->open = false;
}

bool ProcFS::file_exists(const char* path) {
    File tmp; tmp.open = false; tmp.type = FdType::FILE;
    if (!open_file(path, &tmp)) return false;
    close_file(&tmp);
    return true;
}

// ── list_directory ───────────────────────────────────────────────────────────

void ProcFS::list_directory(const char* path) {
    char name[64];
    pt::uint32_t size;
    pt::uint8_t type;
    for (int idx = 0; ; idx++) {
        if (!readdir_ex(path, idx, name, &size, &type)) break;
        if (type == 1)
            vterm_printf("  %s/\n", name);
        else
            vterm_printf("  %s\n", name);
    }
}

// ── readdir_ex ────────────────────────────────────────────────────────────────

int ProcFS::readdir_ex(const char* path, int idx,
                       char* name_out, pt::uint32_t* size_out, pt::uint8_t* type_out)
{
    auto is_empty = [](const char* s) { return !s || !*s; };

    // ── proc root ──────────────────────────────────────────────────────────
    if (is_empty(path)) {
        const char* sys_files[] = { "version", "meminfo", "uptime" };
        constexpr int SYS_COUNT = 3;

        if (idx < SYS_COUNT) {
            const char* name = sys_files[idx];
            int i = 0; while (*name) name_out[i++] = *name++;
            name_out[i] = '\0';
            if (size_out) *size_out = 0;
            if (type_out) *type_out = 0;  // file
            return 1;
        }

        int task_idx = idx - SYS_COUNT;
        int found = 0;
        for (pt::uint32_t i = 0; i < TaskScheduler::MAX_TASKS; i++) {
            Task* t = TaskScheduler::get_task(i);
            if (!t || t->state == TASK_DEAD || t->state == TASK_ZOMBIE) continue;
            if (found == task_idx) {
                pt::uint32_t pid = t->id;
                char tmp[12]; int n = 0;
                if (pid == 0) { tmp[n++] = '0'; }
                else { pt::uint32_t v = pid; while (v) { tmp[n++] = '0' + (int)(v % 10); v /= 10; } }
                for (int j = n - 1, k = 0; j >= 0; j--, k++) name_out[k] = tmp[j];
                name_out[n] = '\0';
                if (size_out) *size_out = 0;
                if (type_out) *type_out = 1;  // directory
                return 1;
            }
            found++;
        }
        return 0;
    }

    // ── proc/<pid> ────────────────────────────────────────────────────────
    pt::uint32_t pid = 0;
    const char* p = path;
    while (*p >= '0' && *p <= '9') pid = pid * 10 + (*p++ - '0');
    if (*p != '\0') return 0;

    Task* t = TaskScheduler::get_task(pid);
    if (!t || t->state == TASK_DEAD) return 0;

    const char* per_task[] = { "status", "maps", "syscalls" };
    constexpr int PT_COUNT = 3;
    if (idx >= PT_COUNT) return 0;

    const char* name = per_task[idx];
    int i = 0; while (*name) name_out[i++] = *name++;
    name_out[i] = '\0';
    if (size_out) *size_out = 0;
    if (type_out) *type_out = 0;  // file
    return 1;
}
