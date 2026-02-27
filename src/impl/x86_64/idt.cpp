#include "idt.h"
#include "com.h"
#include "pipe.h"
#include "vfs.h"
#include "fbterm.h"
#include "framebuffer.h"
#include "kernel.h"
#include "keyboard.h"
#include "pic.h"
#include "rtc.h"
#include "syscall.h"
#include "task.h"
#include "timer.h"
#include "virtual.h"
#include "window.h"
#include "net.h"

extern pt::uintptr_t g_syscall_rsp;
extern IDT64 _idt[256];
extern pt::uint64_t isr0;
extern pt::uint64_t isr1;
extern pt::uint64_t isr2;
extern pt::uint64_t isr3;
extern pt::uint64_t isr4;
extern pt::uint64_t isr5;
extern pt::uint64_t isr6;
extern pt::uint64_t isr7;
extern pt::uint64_t isr8;
extern pt::uint64_t isr9;
extern pt::uint64_t isr10;
extern pt::uint64_t isr11;
extern pt::uint64_t isr12;
extern pt::uint64_t isr13;
extern pt::uint64_t isr14;
extern pt::uint64_t isr15;
extern pt::uint64_t isr16;
extern pt::uint64_t isr17;
extern pt::uint64_t isr18;
extern pt::uint64_t isr19;
extern pt::uint64_t isr20;
extern pt::uint64_t isr21;
extern pt::uint64_t isr22;
extern pt::uint64_t isr23;
extern pt::uint64_t isr24;
extern pt::uint64_t isr25;
extern pt::uint64_t isr26;
extern pt::uint64_t isr27;
extern pt::uint64_t isr28;
extern pt::uint64_t isr29;
extern pt::uint64_t isr30;
extern pt::uint64_t isr31;
extern pt::uint64_t irq0;
extern pt::uint64_t irq1;
extern pt::uint64_t irq11;
extern pt::uint64_t irq12;
extern pt::uint64_t irq14;
extern pt::uint64_t irq15;
extern pt::uint64_t _syscall_stub;
extern pt::uint64_t _int_yield_stub;

extern void keyboard_routine(pt::uint8_t scancode);
extern void mouse_routine(const pt::int8_t mouse[]);
ASMCALL void LoadIDT();

// type_attr = 0x8E: P=1, DPL=0, interrupt gate (ring-0 only)
// type_attr = 0xEE: P=1, DPL=3, interrupt gate (callable from ring-3)
void init_idt_entry(int irq_no, pt::uint64_t& irq, pt::uint8_t type_attr = 0x8e)
{
	_idt[irq_no].zero = 0;
	_idt[irq_no].offset_low  = (pt::uint16_t)((pt::uint64_t)&irq & 0x000000000000FFFF);
	_idt[irq_no].offset_mid  = (pt::uint16_t)(((pt::uint64_t)&irq & 0x00000000FFFF0000) >> 16);
	_idt[irq_no].offset_high = (pt::uint32_t)(((pt::uint64_t)&irq & 0xFFFFFFFF00000000) >> 32);
	_idt[irq_no].ist = 0;
	_idt[irq_no].selector = 0x08;
	_idt[irq_no].type_attr = type_attr;
}

void IDT::initialize()
{
	PIC::Remap();

	init_idt_entry(0, isr0);
	init_idt_entry(1, isr1);
	init_idt_entry(2, isr2);
	init_idt_entry(3, isr3);
	init_idt_entry(4, isr4);
	init_idt_entry(5, isr5);
	init_idt_entry(6, isr6);
	init_idt_entry(7, isr7);
	init_idt_entry(8, isr8);
	init_idt_entry(9, isr9);
	init_idt_entry(10, isr10);
	init_idt_entry(11, isr11);
	init_idt_entry(12, isr12);
	init_idt_entry(13, isr13);
	init_idt_entry(14, isr14);
	init_idt_entry(15, isr15);
	init_idt_entry(16, isr16);
	init_idt_entry(17, isr17);
	init_idt_entry(18, isr18);
	init_idt_entry(19, isr19);
	init_idt_entry(20, isr20);
	init_idt_entry(21, isr21);
	init_idt_entry(22, isr22);
	init_idt_entry(23, isr23);
	init_idt_entry(24, isr24);
	init_idt_entry(25, isr25);
	init_idt_entry(26, isr26);
	init_idt_entry(27, isr27);
	init_idt_entry(28, isr28);
	init_idt_entry(29, isr29);
	init_idt_entry(30, isr30);
	init_idt_entry(31, isr31);

	init_idt_entry(32, irq0);
	init_idt_entry(33, irq1);
	init_idt_entry(43, irq11);   // IRQ11 → vector 43 (32 + 11) — RTL8139 NIC
	init_idt_entry(44, irq12);
	init_idt_entry(46, irq14);
	init_idt_entry(47, irq15);

	init_idt_entry(0x80, _syscall_stub,   0xEE);  // DPL=3: ring-3 can call int 0x80
	init_idt_entry(0x81, _int_yield_stub, 0xEE);  // DPL=3: ring-3 can call int 0x81

	PIC::UnmaskAll();
	LoadIDT();
}

// Called directly from the irq0 asm stub with RSP pointing to the PUSHALL frame.
// Returns the RSP to resume (same task or next task).
ASMCALL pt::uintptr_t irq0_schedule(pt::uintptr_t saved_rsp)
{
	timer_tick();
	PIC::irq_ack(0);
	return TaskScheduler::preempt(saved_rsp);
}

// Called from _int_yield_stub (int 0x81).  Cooperative yield path.
ASMCALL pt::uintptr_t yield_schedule(pt::uintptr_t saved_rsp)
{
	return TaskScheduler::yield_tick(saved_rsp);
}

ASMCALL void irq1_handler()
{
	const pt::uint8_t c = IO::inb(0x60);
	keyboard_routine(c);
	PIC::irq_ack(1);
}

pt::uint8_t mouse_cycle=0;
pt::int8_t  mouse_byte[3];

ASMCALL void irq12_handler()
{
	switch(mouse_cycle)
	{
		case 0:
			mouse_byte[0] = IO::inb(0x60);
			mouse_cycle++;
			break;
		case 1:
			mouse_byte[1] = IO::inb(0x60);
			mouse_cycle++;
			break;
		case 2:
			mouse_byte[2] = IO::inb(0x60);
			mouse_cycle=0;
			mouse_routine(mouse_byte);
			break;
	}
	PIC::irq_ack(12);
}

ASMCALL void irq11_handler()
{
	RTL8139::handle_irq();
	PIC::irq_ack(11);
}

ASMCALL void irq14_handler()
{
	// IDE primary channel interrupt - just acknowledge it
	// We're using polling, so we don't need to do anything here
	PIC::irq_ack(14);
}

ASMCALL void irq15_handler()
{
	// IDE secondary channel interrupt - just acknowledge it
	// We're using polling, so we don't need to do anything here
	PIC::irq_ack(15);
}

ASMCALL void isr0_handler()
{
	kernel_panic("Divide by zero", 0);
}

ASMCALL void isr1_handler()
{
	kernel_panic("Debug", 0);
}

ASMCALL void isr2_handler()
{
	kernel_panic("NMI", 2);
}

ASMCALL void isr3_handler()
{
	kernel_panic("Debug", 3);
}

ASMCALL void isr4_handler()
{
	kernel_panic("Overflow", 4);
}

ASMCALL void isr5_handler()
{
	kernel_panic("Bound range exceeded", 5);
}

ASMCALL void isr6_handler(pt::uint64_t* frame)
{
	// frame[15] = faulting RIP (+120), frame[16] = CS (+128)
	// For ring-3 faults: frame[18] = user RSP (+144), frame[19] = SS (+152)
	pt::uint64_t rip    = frame[15];
	pt::uint64_t cs     = frame[16];
	pt::uint64_t rflags = frame[17];
	pt::uint64_t rsp    = frame[18];
	pt::uint64_t ss     = frame[19];
	pt::uint64_t rbp    = frame[10];  // saved rbp: PUSHALL index 10 (+80)
	klog("[ISR6] #UD at RIP=%lx CS=%lx RFLAGS=%lx RSP=%lx SS=%lx rbp=%lx (ring-%d)\n",
	     rip, cs, rflags, rsp, ss, rbp, (int)(cs & 3));
	// Dump opcodes at the faulting address.
	const pt::uint8_t* code = reinterpret_cast<const pt::uint8_t*>(rip);
	klog("[ISR6] opcodes @ RIP: %x %x %x %x %x %x %x %x\n",
	     (unsigned)code[0], (unsigned)code[1], (unsigned)code[2], (unsigned)code[3],
	     (unsigned)code[4], (unsigned)code[5], (unsigned)code[6], (unsigned)code[7]);
	kernel_panic("Invalid opcode", 6);
}

ASMCALL void isr7_handler()
{
	kernel_panic("Device not available", 7);
}

ASMCALL void isr9_handler()
{
	kernel_panic("Coprocessor segment overrun", 9);
}

ASMCALL void isr10_handler()
{
	kernel_panic("Invalid TSS", 10);
}

ASMCALL void isr11_handler()
{
	kernel_panic("Segment not present", 11);
}

ASMCALL void isr12_handler()
{
	kernel_panic("Stack segment fault", 12);
}

ASMCALL void isr8_handler()
{
	kernel_panic("Double fault", 8);
}

ASMCALL void isr13_handler(pt::uint64_t* frame)
{
	// frame[14] = rax = error code (popped before PUSHALL, stored at +112)
	// frame[15] = faulting RIP (+120)
	// frame[16] = CS (+128)
	// frame[17] = RFLAGS (+136)
	// frame[18] = user RSP (+144) — only valid if ring change (CS & 3)
	// frame[19] = SS (+152)       — only valid if ring change
	pt::uint64_t err_code = frame[14];
	pt::uint64_t rip      = frame[15];
	pt::uint64_t cs       = frame[16];
	pt::uint64_t rflags   = frame[17];
	klog("[ISR13] #GP errcode=%lx RIP=%lx CS=%lx RFLAGS=%lx (ring-%d)\n",
	     err_code, rip, cs, rflags, (int)(cs & 3));
	// Dump opcodes at the faulting address
	const pt::uint8_t* code = reinterpret_cast<const pt::uint8_t*>(rip);
	klog("[ISR13] opcodes @ RIP: %x %x %x %x %x %x %x %x\n",
	     (unsigned)code[0], (unsigned)code[1], (unsigned)code[2], (unsigned)code[3],
	     (unsigned)code[4], (unsigned)code[5], (unsigned)code[6], (unsigned)code[7]);
	// PUSHALL order (idt.asm): rax rbx rcx rdx rbp rsi rdi r8..r15
	// After PUSHALL, frame[0]=r15 [1]=r14 [2]=r13 [3]=r12 [4]=r11 [5]=r10
	// [6]=r9 [7]=r8 [8]=rdi [9]=rsi [10]=rbp [11]=rdx [12]=rcx [13]=rbx
	// [14]=rax(=errcode for isr13) [15]=RIP [16]=CS [17]=RFLAGS [18]=RSP [19]=SS
	klog("[ISR13] rdi=%lx rsi=%lx rbp=%lx rdx=%lx rcx=%lx rbx=%lx\n",
	     frame[8], frame[9], frame[10], frame[11], frame[12], frame[13]);
	if (cs & 3) {
		pt::uint64_t rsp = frame[18];
		pt::uint64_t ss  = frame[19];
		klog("[ISR13] ring-3 RSP=%lx SS=%lx\n", rsp, ss);
	}
	kernel_panic("General protection fault", 13);
}

ASMCALL void isr14_handler(pt::uintptr_t frame)
{
	pt::uintptr_t cr2;
	asm __volatile__("mov %%cr2, %0" : "=r"(cr2));
	pt::uint64_t err_code = *reinterpret_cast<pt::uint64_t*>(frame + 112);
	pt::uint64_t user_rip = *reinterpret_cast<pt::uint64_t*>(frame + 120);
	pt::uint64_t user_rsp = *reinterpret_cast<pt::uint64_t*>(frame + 144);
	pt::uint64_t user_rbp = *reinterpret_cast<pt::uint64_t*>(frame + 80);
	pt::uint64_t user_rdi = *reinterpret_cast<pt::uint64_t*>(frame + 64);
	pt::uint64_t user_rsi = *reinterpret_cast<pt::uint64_t*>(frame + 72);
	pt::uint64_t user_rdx = *reinterpret_cast<pt::uint64_t*>(frame + 88);
	pt::uint64_t user_rcx = *reinterpret_cast<pt::uint64_t*>(frame + 96);
	pt::uint64_t user_rbx = *reinterpret_cast<pt::uint64_t*>(frame + 104);
	klog("[PAGE FAULT] cr2=%lx errcode=%lx RIP=%lx RSP=%lx\n", cr2, err_code, user_rip, user_rsp);
	klog("[PAGE FAULT] rbp=%lx rbx=%lx\n", user_rbp, user_rbx);
	klog("[PAGE FAULT] rdi=%lx rsi=%lx rdx=%lx rcx=%lx\n", user_rdi, user_rsi, user_rdx, user_rcx);

	kernel_panic("Page fault", 14);
}

ASMCALL void isr15_handler()
{
	kernel_panic("Reserved exception 15", 15);
}

ASMCALL void isr16_handler()
{
	kernel_panic("Floating point exception", 16);
}

ASMCALL void isr17_handler()
{
	kernel_panic("Alignment check", 17);
}

ASMCALL void isr18_handler()
{
	kernel_panic("Machine check", 18);
}

ASMCALL void isr19_handler()
{
	kernel_panic("SIMD floating point exception", 19);
}

ASMCALL void isr20_handler()
{
	kernel_panic("Reserved exception 20", 20);
}

ASMCALL void isr21_handler()
{
	kernel_panic("Reserved exception 21", 21);
}

ASMCALL void isr22_handler()
{
	kernel_panic("Reserved exception 22", 22);
}

ASMCALL void isr23_handler()
{
	kernel_panic("Reserved exception 23", 23);
}

ASMCALL void isr24_handler()
{
	kernel_panic("Reserved exception 24", 24);
}

ASMCALL void isr25_handler()
{
	kernel_panic("Reserved exception 25", 25);
}

ASMCALL void isr26_handler()
{
	kernel_panic("Reserved exception 26", 26);
}

ASMCALL void isr27_handler()
{
	kernel_panic("Reserved exception 27", 27);
}

ASMCALL void isr28_handler()
{
	kernel_panic("Reserved exception 28", 28);
}

ASMCALL void isr29_handler()
{
	kernel_panic("Reserved exception 29", 29);
}

ASMCALL void isr30_handler()
{
	kernel_panic("Reserved exception 30", 30);
}

ASMCALL void isr31_handler()
{
	kernel_panic("Reserved exception 31", 31);
}

ASMCALL pt::uint64_t syscall_handler(pt::uint64_t nr, pt::uint64_t arg1,
                                      pt::uint64_t arg2, pt::uint64_t arg3,
                                      pt::uint64_t arg4, pt::uint64_t arg5)
{
	// Snapshot g_syscall_rsp into the current task BEFORE any blocking
	// operation (e.g. waitpid) can cause another task's SYS_EXIT to
	// overwrite the global with a different task's kernel stack RSP.
	{
		Task* ct = TaskScheduler::get_current_task();
		if (ct) ct->syscall_frame_rsp = g_syscall_rsp;
	}

	switch (nr) {
		case SYS_WRITE: {
			int fd = (int)(pt::int8_t)arg1;
			const char* buf = reinterpret_cast<const char*>(arg2);
			pt::uint32_t n  = (pt::uint32_t)arg3;
			if (fd == 1) {
				// stdout: windowed tasks render text into their client area;
				// tasks without a window use the global fbterm as before.
				Task* wt = TaskScheduler::get_current_task();
				bool has_window = wt && wt->window_id != INVALID_WID;
				for (pt::uint32_t i = 0; i < n; i++) {
					if (has_window) {
						WindowManager::put_char(wt->window_id, buf[i]);
					} else {
						fbterm.put_char(buf[i]);
						// mirror to serial only for non-windowed tasks;
						// windowed tasks have their own display and their
						// output (incl. ANSI sequences) must not pollute the log.
						char tmp[2] = { buf[i], '\0' };
						debug.print_str(tmp);
					}
				}
				return (pt::uint64_t)n;
			}
			Task* t = TaskScheduler::get_current_task();
			if (fd < 0 || fd >= (int)Task::MAX_FDS || !t->fd_table[fd].open)
				return (pt::uint64_t)-1;
			{
				File* f = &t->fd_table[fd];
				if (f->type == FdType::FILE)
					return (pt::uint64_t)VFS::write_file(f, buf, n);
				if (f->type == FdType::PIPE_WR) {
					PipeBuffer* pipe = pipe_get_buf(f->fs_data);
					pt::uint32_t written = 0;
					while (written < n) {
						pt::uint32_t used = pipe->write_pos - pipe->read_pos;
						if (used < PipeBuffer::CAPACITY) {
							pipe->data[pipe->write_pos % PipeBuffer::CAPACITY] =
								(pt::uint8_t)buf[written++];
							pipe->write_pos++;
						} else if (pipe->ref_count <= 1) {
							break;  // reader gone (broken pipe) — stop writing
						} else {
							TaskScheduler::task_yield();
						}
					}
					return (pt::uint64_t)written;
				}
			}
			return (pt::uint64_t)-1;
		}
		case SYS_EXIT:
			TaskScheduler::task_exit((int)arg1);
			return 0;
		case SYS_READ_KEY: {
			// Windowed tasks receive keyboard events via their per-window queue
			// so that the kernel shell's polling loop cannot steal their input.
			Task* wt = TaskScheduler::get_current_task();
			if (wt && wt->window_id != INVALID_WID) {
				pt::uint64_t ev = WindowManager::poll_event(wt->window_id);
				if (ev == 0) return (pt::uint64_t)-1;   // queue empty
				bool pressed = (ev & 0x100) != 0;
				if (!pressed) return (pt::uint64_t)-1;  // skip key-release events
				pt::uint8_t sc = (pt::uint8_t)(ev & 0xFF);
				char ch = keyboard_scancode_to_char(sc);
				if (ch == 0) return (pt::uint64_t)-1;   // non-printable / modifier
				return (pt::uint64_t)(pt::uint8_t)ch;
			}
			const char c = get_char();
			// get_char() returns -1 (as char) when no key is available.
			if (c == -1)
				return (pt::uint64_t)-1;
			return (pt::uint64_t)(pt::uint8_t)c;
		}
		case SYS_OPEN: {
			const char* filename = reinterpret_cast<const char*>(arg1);
			Task* t = TaskScheduler::get_current_task();
			// Find a free fd slot; 0/1/2 are reserved for stdin/stdout/stderr.
			int fd = -1;
			for (int i = 3; i < (int)Task::MAX_FDS; i++) {
				if (!t->fd_table[i].open) { fd = i; break; }
			}
			if (fd == -1) {
				klog("syscall: SYS_OPEN: no free fd\n");
				return (pt::uint64_t)-1;
			}
			if (!VFS::open_file(filename, &t->fd_table[fd])) {
				klog("syscall: SYS_OPEN: '%s' not found\n", filename);
				return (pt::uint64_t)-1;
			}
			t->fd_table[fd].type = FdType::FILE;
			klog("syscall: SYS_OPEN: '%s' -> fd %d\n", filename, fd);
			return (pt::uint64_t)fd;
		}
		case SYS_READ: {
			int fd = (int)(pt::int8_t)arg1;  // treat as signed to catch negative fds
			void* buf        = reinterpret_cast<void*>(arg2);
			pt::uint32_t count = (pt::uint32_t)arg3;
			Task* t = TaskScheduler::get_current_task();
			if (fd < 0 || fd >= (int)Task::MAX_FDS || !t->fd_table[fd].open)
				return (pt::uint64_t)-1;
			File* f = &t->fd_table[fd];
			if (f->type == FdType::FILE)
				return (pt::uint64_t)VFS::read_file(f, buf, count);
			if (f->type == FdType::PIPE_RD) {
				PipeBuffer* pipe = pipe_get_buf(f->fs_data);
				pt::uint32_t nread = 0;
				pt::uint8_t* dst   = reinterpret_cast<pt::uint8_t*>(buf);
				while (nread < count) {
					if (pipe->write_pos != pipe->read_pos) {
						dst[nread++] = pipe->data[pipe->read_pos % PipeBuffer::CAPACITY];
						pipe->read_pos++;
					} else if (pipe->writer_closed || pipe->ref_count <= 1) {
						break;  // EOF: writer closed or all writers gone
					} else {
						TaskScheduler::task_yield();
					}
				}
				return (pt::uint64_t)nread;
			}
			return (pt::uint64_t)-1;
		}
		case SYS_CLOSE: {
			int fd = (int)(pt::int8_t)arg1;
			Task* t = TaskScheduler::get_current_task();
			if (fd < 0 || fd >= (int)Task::MAX_FDS || !t->fd_table[fd].open)
				return (pt::uint64_t)-1;
			File* f = &t->fd_table[fd];
			if (f->type == FdType::FILE) {
				VFS::close_file(f);
			} else {
				// PIPE_RD or PIPE_WR
				PipeBuffer* pipe = pipe_get_buf(f->fs_data);
				if (f->type == FdType::PIPE_WR)
					pipe->writer_closed = true;
				pipe->ref_count--;
				if (pipe->ref_count == 0)
					vmm.kfree(pipe);
				f->open = false;
			}
			klog("syscall: SYS_CLOSE: fd %d\n", fd);
			return 0;
		}
		case SYS_MMAP: {
			pt::size_t size = (pt::size_t)arg1;
			if (size == 0) return (pt::uint64_t)-1;
			void* ptr = vmm.kmalloc(size);
			if (!ptr) return (pt::uint64_t)-1;
			klog("syscall: SYS_MMAP size=%d -> %lx\n", (int)size, (pt::uint64_t)ptr);
			return (pt::uint64_t)ptr;
		}
		case SYS_MUNMAP: {
			void* ptr = reinterpret_cast<void*>(arg1);
			if (!ptr) return (pt::uint64_t)-1;
			vmm.kfree(ptr);
			klog("syscall: SYS_MUNMAP ptr=%lx\n", (pt::uint64_t)ptr);
			return 0;
		}
		case SYS_YIELD:
			TaskScheduler::task_yield();
			return 0;
		case SYS_GET_TICKS:
			return (pt::uint64_t)get_ticks();
		case SYS_GET_TIME: {
			RTCTime t;
			rtc_read(&t);
			return ((pt::uint64_t)t.hours << 8) | t.minutes;
		}
		case SYS_FILL_RECT: {
			Framebuffer* fb = Framebuffer::get_instance();
			if (!fb) return (pt::uint64_t)-1;
			pt::uint8_t r = (pt::uint8_t)(arg5 >> 16);
			pt::uint8_t g = (pt::uint8_t)(arg5 >> 8);
			pt::uint8_t b = (pt::uint8_t)(arg5);
			{
				Task* t = TaskScheduler::get_current_task();
				if (t && t->window_id != INVALID_WID) {
					pt::uint32_t sx, sy, sw, sh;
					if (!WindowManager::translate_rect(t->window_id,
					        (pt::uint32_t)arg1, (pt::uint32_t)arg2,
					        (pt::uint32_t)arg3, (pt::uint32_t)arg4,
					        sx, sy, sw, sh))
						return 0;
					arg1 = sx; arg2 = sy; arg3 = sw; arg4 = sh;
				}
			}
			fb->FillRect((pt::uint32_t)arg1, (pt::uint32_t)arg2,
			             (pt::uint32_t)arg3, (pt::uint32_t)arg4, r, g, b);
			// Z-order: chromeless windows draw behind normal window chrome.
			{
				Task* ct = TaskScheduler::get_current_task();
				if (ct && ct->window_id != INVALID_WID) {
					Window* cw = WindowManager::get_window(ct->window_id);
					if (cw && cw->chromeless)
						WindowManager::redraw_all_chrome();
				}
			}
			return 0;
		}
		case SYS_DRAW_TEXT: {
			{
				Task* t = TaskScheduler::get_current_task();
				if (t && t->window_id != INVALID_WID) {
					pt::uint32_t sx, sy;
					if (!WindowManager::translate_point(t->window_id,
					        (pt::uint32_t)arg1, (pt::uint32_t)arg2, sx, sy))
						return 0;
					arg1 = sx; arg2 = sy;
				}
			}
			if (fbterm.is_ready())
				fbterm.draw_at((pt::uint32_t)arg1, (pt::uint32_t)arg2,
				               reinterpret_cast<const char*>(arg3),
				               (pt::uint32_t)arg4, (pt::uint32_t)arg5);
			// Z-order: chromeless windows draw behind normal window chrome.
			{
				Task* ct = TaskScheduler::get_current_task();
				if (ct && ct->window_id != INVALID_WID) {
					Window* cw = WindowManager::get_window(ct->window_id);
					if (cw && cw->chromeless)
						WindowManager::redraw_all_chrome();
				}
			}
			return 0;
		}
		case SYS_FB_WIDTH: {
			Framebuffer* fb = Framebuffer::get_instance();
			pt::uint64_t w = fb ? (pt::uint64_t)fb->get_width() : 0;
			return w;
		}
		case SYS_FORK: {
			Task* ct = TaskScheduler::get_current_task();
			return (pt::uint64_t)TaskScheduler::fork_task(
				ct ? ct->syscall_frame_rsp : g_syscall_rsp);
		}

		case SYS_EXEC: {
			Task* ct = TaskScheduler::get_current_task();
			pt::uintptr_t frame_rsp = ct ? ct->syscall_frame_rsp : g_syscall_rsp;
			return TaskScheduler::exec_task(
				reinterpret_cast<const char*>(arg1), frame_rsp);
		}

		case SYS_WAITPID: {
			pt::uint64_t wr = TaskScheduler::waitpid_task(
				(pt::uint32_t)arg1,
				reinterpret_cast<int*>(arg2));
#ifdef FORK_DEBUG
			Task* ct = TaskScheduler::get_current_task();
			pt::uintptr_t frame_rsp = ct ? ct->syscall_frame_rsp : g_syscall_rsp;
			klog("[SYSCALL_DEBUG] SYS_WAITPID -> %llu iretq: RIP=%lx RSP=%lx\n",
			     wr,
			     *(pt::uint64_t*)(frame_rsp + 120),
			     *(pt::uint64_t*)(frame_rsp + 144));
#endif
			return wr;
		}

		case SYS_PIPE: {
			int* pipefd = reinterpret_cast<int*>(arg1);
			Task* t = TaskScheduler::get_current_task();
			// Find two free fd slots; 0/1/2 are reserved for stdin/stdout/stderr.
			int rd_fd = -1, wr_fd = -1;
			for (int i = 3; i < (int)Task::MAX_FDS && (rd_fd == -1 || wr_fd == -1); i++) {
				if (!t->fd_table[i].open) {
					if (rd_fd == -1) rd_fd = i;
					else             wr_fd = i;
				}
			}
			if (rd_fd == -1 || wr_fd == -1) {
				klog("syscall: SYS_PIPE: no free fd slots\n");
				return (pt::uint64_t)-1;
			}
			// Allocate and zero-init a PipeBuffer.
			PipeBuffer* pipe = reinterpret_cast<PipeBuffer*>(vmm.kcalloc(sizeof(PipeBuffer)));
			if (!pipe) {
				klog("syscall: SYS_PIPE: out of memory\n");
				return (pt::uint64_t)-1;
			}
			pipe->ref_count     = 2;
			pipe->writer_closed = false;
			pipe->read_pos      = 0;
			pipe->write_pos     = 0;
			// Set up read end.
			File* rd = &t->fd_table[rd_fd];
			rd->open = true;
			rd->type = FdType::PIPE_RD;
			pipe_set_buf(rd->fs_data, pipe);
			// Set up write end.
			File* wr = &t->fd_table[wr_fd];
			wr->open = true;
			wr->type = FdType::PIPE_WR;
			pipe_set_buf(wr->fs_data, pipe);
			// Return fds to caller.
			pipefd[0] = rd_fd;
			pipefd[1] = wr_fd;
			klog("syscall: SYS_PIPE: rd=%d wr=%d\n", rd_fd, wr_fd);
			return 0;
		}

		case SYS_LSEEK: {
			int fd = (int)(pt::int8_t)arg1;
			pt::int32_t offset = (pt::int32_t)(pt::int64_t)arg2;
			int whence = (int)arg3;
			Task* t = TaskScheduler::get_current_task();
			if (fd < 0 || fd >= (int)Task::MAX_FDS || !t->fd_table[fd].open)
				return (pt::uint64_t)-1;
			File* f = &t->fd_table[fd];
			if (f->type != FdType::FILE) return (pt::uint64_t)-1;
			return (pt::uint64_t)VFS::seek_file(f, offset, whence);
		}

		case SYS_FB_HEIGHT: {
			Framebuffer* fb = Framebuffer::get_instance();
			return fb ? (pt::uint64_t)fb->get_height() : 0;
		}

		case SYS_DRAW_PIXELS: {
			Framebuffer* fb = Framebuffer::get_instance();
			if (!fb) return (pt::uint64_t)-1;
			const pt::uint8_t* buf = reinterpret_cast<const pt::uint8_t*>(arg1);
			// Rate-limited log: every 100 frames, log call count + first pixel
			{
				static pt::uint32_t dp_count = 0;
				dp_count++;
				if (dp_count % 100 == 1) {
					klog("[DRAW_PIXELS] call#%d x=%d y=%d w=%d h=%d px0=[%x,%x,%x]\n",
					     (int)dp_count, (int)arg2, (int)arg3,
					     (int)arg4, (int)arg5,
					     (unsigned)buf[0], (unsigned)buf[1], (unsigned)buf[2]);
				}
			}
			{
				Task* t = TaskScheduler::get_current_task();
				if (t && t->window_id != INVALID_WID) {
					pt::uint32_t sx, sy, sw, sh;
					if (!WindowManager::translate_rect(t->window_id,
					        (pt::uint32_t)arg2, (pt::uint32_t)arg3,
					        (pt::uint32_t)arg4, (pt::uint32_t)arg5,
					        sx, sy, sw, sh))
						return 0;
					arg2 = sx; arg3 = sy;
					// arg4/arg5 (w/h) keep original values to preserve source stride
				}
			}
			fb->Draw(buf, (pt::uint32_t)arg2, (pt::uint32_t)arg3,
			         (pt::uint32_t)arg4, (pt::uint32_t)arg5);
			return 0;
		}

		case SYS_GET_KEY_EVENT: {
			KeyEvent ev;
			if (!get_key_event(&ev))
				return (pt::uint64_t)-1;
			// Encoding: bit 8 = pressed, bits 7:0 = PS/2 scancode.
			return (pt::uint64_t)ev.scancode | (ev.pressed ? 0x100u : 0u);
		}

		case SYS_CREATE: {
			const char* filename = reinterpret_cast<const char*>(arg1);
			Task* t = TaskScheduler::get_current_task();
			// Find a free fd slot; 0/1/2 are reserved for stdin/stdout/stderr.
			int fd = -1;
			for (int i = 3; i < (int)Task::MAX_FDS; i++) {
				if (!t->fd_table[i].open) { fd = i; break; }
			}
			if (fd == -1) {
				klog("syscall: SYS_CREATE: no free fd\n");
				return (pt::uint64_t)-1;
			}
			if (!VFS::open_file_write(filename, &t->fd_table[fd])) {
				klog("syscall: SYS_CREATE: '%s' failed\n", filename);
				return (pt::uint64_t)-1;
			}
			t->fd_table[fd].type = FdType::FILE;
			klog("syscall: SYS_CREATE: '%s' -> fd %d\n", filename, fd);
			return (pt::uint64_t)fd;
		}

		case SYS_SLEEP:
			TaskScheduler::sleep_task(arg1);
			return 0;

		case SYS_CREATE_WINDOW: {
			Task* t = TaskScheduler::get_current_task();
			if (!t || t->window_id != INVALID_WID) return (pt::uint64_t)-1;
			pt::uint32_t wid = WindowManager::create_window(
			    (pt::uint32_t)arg1, (pt::uint32_t)arg2,
			    (pt::uint32_t)arg3, (pt::uint32_t)arg4, t->id,
			    (pt::uint32_t)arg5);
			if (wid == INVALID_WID) return (pt::uint64_t)-1;
			t->window_id = wid;
			return wid;
		}
		case SYS_DESTROY_WINDOW: {
			Task* t = TaskScheduler::get_current_task();
			Window* w = WindowManager::get_window((pt::uint32_t)arg1);
			if (!t || !w || w->owner_task_id != t->id) return (pt::uint64_t)-1;
			WindowManager::destroy_window((pt::uint32_t)arg1);
			t->window_id = INVALID_WID;
			return 0;
		}
		case SYS_GET_WINDOW_EVENT: {
			Task* t = TaskScheduler::get_current_task();
			Window* w = WindowManager::get_window((pt::uint32_t)arg1);
			if (!t || !w || w->owner_task_id != t->id) return 0;
			return WindowManager::poll_event((pt::uint32_t)arg1);
		}

		case SYS_READDIR: {
			int idx          = (int)arg1;
			char* name       = reinterpret_cast<char*>(arg2);
			pt::uint32_t* sz = reinterpret_cast<pt::uint32_t*>(arg3);
			return VFS::readdir(idx, name, sz) ? 1 : 0;
		}

		case SYS_MEM_FREE:
			return (pt::uint64_t)vmm.memsize();

		case SYS_DISK_SIZE:
			return (pt::uint64_t)VFS::get_total_space();

		case SYS_REMOVE: {
			const char* filename = reinterpret_cast<const char*>(arg1);
			return VFS::delete_file(filename) ? 0 : (pt::uint64_t)-1;
		}

		default:
			klog("syscall: unknown nr=%llu\n", nr);
			return (pt::uint64_t)-1;
	}
}