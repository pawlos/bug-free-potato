#include "device/ac97.h"
#include "device/com.h"
#include "device/mouse.h"
#include "pipe.h"
#include "fs/vfs.h"
#include "device/fbterm.h"
#include "framebuffer.h"
#include "kernel.h"
#include "device/keyboard.h"
#include "device/rtc.h"
#include "syscall.h"
#include "task.h"
#include "device/timer.h"
#include "virtual.h"
#include "window.h"
#include "net/net.h"
#include "vterm.h"

extern pt::uintptr_t g_syscall_rsp;

// Per-syscall trace logging. Compile with -DSYSCALL_LOG to enable the noisy
// "syscall: SYS_..." messages (SYS_OPEN/SYS_CLOSE/SYS_MMAP and friends).
// Without it the syscall dispatcher stays quiet while other subsystems can
// still use klog(). Only use for debugging — it floods the serial log fast.
#ifdef SYSCALL_LOG
#define sclog(...) klog(__VA_ARGS__)
#else
#define sclog(...) ((void)0)
#endif

static pt::uint64_t syscall_dispatch(pt::uint64_t nr, pt::uint64_t arg1,
                                      pt::uint64_t arg2, pt::uint64_t arg3,
                                      pt::uint64_t arg4, pt::uint64_t arg5)
{
	switch (nr) {
		case SYS_WRITE: {
			int fd = (int)(pt::int8_t)arg1;
			const char* buf = reinterpret_cast<const char*>(arg2);
			pt::uint32_t n  = (pt::uint32_t)arg3;
			if (fd == 1) {
				Task* wt = TaskScheduler::get_current_task();
				bool has_window = wt && wt->window_id != INVALID_WID;
				bool has_vterm  = wt && wt->vterm_id  != INVALID_VT;
				VTerm* vt = nullptr;
				if (!has_window) {
					vt = has_vterm ? vterm_get(wt->vterm_id) : vterm_active();
					if (vt) vt->begin_batch();
				}
				for (pt::uint32_t i = 0; i < n; i++) {
					if (has_window) {
						WindowManager::put_char(wt->window_id, buf[i]);
					} else if (vt) {
						vt->put_char(buf[i]);
					}
				}
				if (vt) vt->end_batch();
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
				if (f->type == FdType::TCP_SOCK) {
					TcpSocket* sock = tcp_sock_get(f->fs_data);
					return (pt::uint64_t)tcp_write(sock, (const pt::uint8_t*)buf, n);
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
			if (wt && wt->vterm_id != INVALID_VT) {
				char c = vterm_get(wt->vterm_id)->pop_input();
				if (c == (char)-1) return (pt::uint64_t)-1;
				return (pt::uint64_t)(pt::uint8_t)c;
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
				sclog("syscall: SYS_OPEN: no free fd (task %d '%s')\n", t->id, t->name);
				return (pt::uint64_t)-1;
			}
			if (!VFS::open_file(filename, &t->fd_table[fd])) {
				sclog("syscall: SYS_OPEN: '%s' not found (task %d '%s')\n", filename, t->id, t->name);
				return (pt::uint64_t)-1;
			}
			t->fd_table[fd].type = FdType::FILE;
			sclog("syscall: SYS_OPEN: '%s' -> fd %d\n", filename, fd);
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
			if (f->type == FdType::TCP_SOCK) {
				TcpSocket* sock = tcp_sock_get(f->fs_data);
				return (pt::uint64_t)tcp_read(sock, (pt::uint8_t*)buf, count, 500);
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
			} else if (f->type == FdType::TCP_SOCK) {
				tcp_close(tcp_sock_get(f->fs_data));
				f->open = false;
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
			sclog("syscall: SYS_CLOSE: fd %d\n", fd);
			return 0;
		}
		case SYS_MMAP: {
			Task* ct = TaskScheduler::get_current_task();
			pt::size_t size = ((pt::size_t)arg1 + 4095) & ~(pt::size_t)4095;
			if (!ct || size == 0) return (pt::uint64_t)-1;
			pt::uintptr_t va = ct->user_heap_top;
			TaskScheduler::map_user_pages(ct, va, size);
			ct->user_heap_top += size;
			sclog("syscall: SYS_MMAP size=%d -> va=%lx\n", (int)size, va);
			return va;
		}
		case SYS_MUNMAP: {
			Task* ct = TaskScheduler::get_current_task();
			pt::uintptr_t va = (pt::uintptr_t)arg1;
			pt::size_t size = ((pt::size_t)arg2 + 4095) & ~(pt::size_t)4095;
			if (!ct || !ct->user_pd || va < TaskScheduler::USER_HEAP_BASE || size == 0)
				return (pt::uint64_t)-1;
			pt::uint64_t* upd = (pt::uint64_t*)(KERNEL_OFFSET + ct->user_pd);
			for (pt::uintptr_t addr = va; addr < va + size; addr += 4096) {
				pt::size_t pd_idx = (addr >> 21) & 0x1FF;
				pt::size_t pt_idx = (addr >> 12) & 0x1FF;
				if (!(upd[pd_idx] & 0x01) || (upd[pd_idx] & 0x80)) continue;
				static constexpr pt::uintptr_t PTE_ADDR_MASK = 0x000FFFFFFFFFF000ULL;
				pt::uint64_t* pt = (pt::uint64_t*)(KERNEL_OFFSET + (upd[pd_idx] & PTE_ADDR_MASK));
				if (pt[pt_idx] & 0x01) {
					vmm.free_frame(pt[pt_idx] & PTE_ADDR_MASK);
					pt[pt_idx] = 0;
					asm volatile("invlpg [%0]" : : "r"(addr) : "memory");
				}
			}
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
			// Byte layout: [31:24]=year-2000 [23:16]=month [15:8]=hours [7:0]=minutes
			// Day in bits [35:32] (next nibble above year)
			return ((pt::uint64_t)t.day << 32)
			     | ((pt::uint64_t)(t.year - 2000) << 24)
			     | ((pt::uint64_t)t.month << 16)
			     | ((pt::uint64_t)t.hours << 8)
			     | t.minutes;
		}
		case SYS_FILL_RECT: {
			pt::uint32_t color = (pt::uint32_t)arg5;
			{
				Task* t = TaskScheduler::get_current_task();
				if (t && t->window_id != INVALID_WID) {
					WindowManager::win_fill_rect(t->window_id,
					    (pt::uint32_t)arg1, (pt::uint32_t)arg2,
					    (pt::uint32_t)arg3, (pt::uint32_t)arg4, color);
					return 0;
				}
			}
			Framebuffer* fb = Framebuffer::get_instance();
			if (!fb) return (pt::uint64_t)-1;
			pt::uint8_t r = (pt::uint8_t)(color >> 16);
			pt::uint8_t g = (pt::uint8_t)(color >> 8);
			pt::uint8_t b = (pt::uint8_t)(color);
			fb->FillRect((pt::uint32_t)arg1, (pt::uint32_t)arg2,
			             (pt::uint32_t)arg3, (pt::uint32_t)arg4, r, g, b);
			return 0;
		}
		case SYS_DRAW_TEXT: {
			{
				Task* t = TaskScheduler::get_current_task();
				if (t && t->window_id != INVALID_WID) {
					WindowManager::win_draw_text(t->window_id,
					    (pt::uint32_t)arg1, (pt::uint32_t)arg2,
					    reinterpret_cast<const char*>(arg3),
					    (pt::uint32_t)arg4, (pt::uint32_t)arg5);
					return 0;
				}
			}
			if (fbterm.is_ready())
				fbterm.draw_at((pt::uint32_t)arg1, (pt::uint32_t)arg2,
				               reinterpret_cast<const char*>(arg3),
				               (pt::uint32_t)arg4, (pt::uint32_t)arg5);
			return 0;
		}
		case SYS_FB_WIDTH: {
			Task* ct = TaskScheduler::get_current_task();
			if (ct && ct->window_id != INVALID_WID) {
				Window* cw = WindowManager::get_window(ct->window_id);
				if (cw) return (pt::uint64_t)cw->client_w;
			}
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
			int exec_argc = (int)arg2;
			const char* const* exec_argv = reinterpret_cast<const char* const*>(arg3);
			return TaskScheduler::exec_task(
				reinterpret_cast<const char*>(arg1), frame_rsp,
				exec_argc, exec_argv);
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
				sclog("syscall: SYS_PIPE: no free fd slots\n");
				return (pt::uint64_t)-1;
			}
			// Allocate and zero-init a PipeBuffer.
			PipeBuffer* pipe = reinterpret_cast<PipeBuffer*>(vmm.kcalloc(sizeof(PipeBuffer)));
			if (!pipe) {
				sclog("syscall: SYS_PIPE: out of memory\n");
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
			sclog("syscall: SYS_PIPE: rd=%d wr=%d\n", rd_fd, wr_fd);
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
			Task* ct = TaskScheduler::get_current_task();
			if (ct && ct->window_id != INVALID_WID) {
				Window* cw = WindowManager::get_window(ct->window_id);
				if (cw) return (pt::uint64_t)cw->client_h;
			}
			Framebuffer* fb = Framebuffer::get_instance();
			return fb ? (pt::uint64_t)fb->get_height() : 0;
		}

		case SYS_DRAW_PIXELS: {
			const pt::uint8_t* buf = reinterpret_cast<const pt::uint8_t*>(arg1);
			{
				Task* t = TaskScheduler::get_current_task();
				if (t && t->window_id != INVALID_WID) {
					WindowManager::win_draw_pixels(t->window_id, buf,
					    (pt::uint32_t)arg2, (pt::uint32_t)arg3,
					    (pt::uint32_t)arg4, (pt::uint32_t)arg5);
					return 0;
				}
			}
			Framebuffer* fb = Framebuffer::get_instance();
			if (!fb) return (pt::uint64_t)-1;
			fb->Draw(buf, (pt::uint32_t)arg2, (pt::uint32_t)arg3,
			         (pt::uint32_t)arg4, (pt::uint32_t)arg5);
			return 0;
		}

		case SYS_GET_KEY_EVENT: {
			// For windowed tasks, read from the window event queue
			// (same encoding: bit 8 = pressed, bits 7:0 = scancode).
			Task* kt = TaskScheduler::get_current_task();
			if (kt && kt->window_id != INVALID_WID) {
				pt::uint64_t wev = WindowManager::poll_event(kt->window_id);
				return wev ? wev : (pt::uint64_t)-1;
			}
			KeyEvent ev;
			if (!get_key_event(&ev))
				return (pt::uint64_t)-1;
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
				sclog("syscall: SYS_CREATE: no free fd (task %d '%s')\n", t->id, t->name);
				return (pt::uint64_t)-1;
			}
			if (!VFS::open_file_write(filename, &t->fd_table[fd])) {
				sclog("syscall: SYS_CREATE: '%s' failed\n", filename);
				return (pt::uint64_t)-1;
			}
			t->fd_table[fd].type = FdType::FILE;
			sclog("syscall: SYS_CREATE: '%s' -> fd %d\n", filename, fd);
			return (pt::uint64_t)fd;
		}

		case SYS_SLEEP:
			TaskScheduler::sleep_task(arg1);
			return 0;

		case SYS_CREATE_WINDOW: {
			Task* t = TaskScheduler::get_current_task();
			if (!t || (t->window_id != INVALID_WID && t->owns_window)) return (pt::uint64_t)-1;
			pt::uint32_t wid = WindowManager::create_window(
			    (pt::uint32_t)arg1, (pt::uint32_t)arg2,
			    (pt::uint32_t)arg3, (pt::uint32_t)arg4, t->id,
			    (pt::uint32_t)arg5);
			if (wid == INVALID_WID) return (pt::uint64_t)-1;
			t->window_id = wid;
			t->owns_window = true;
			return wid;
		}
		case SYS_DESTROY_WINDOW: {
			Task* t = TaskScheduler::get_current_task();
			Window* w = WindowManager::get_window((pt::uint32_t)arg1);
			if (!t || !w || w->owner_task_id != t->id) return (pt::uint64_t)-1;
			WindowManager::destroy_window((pt::uint32_t)arg1);
			t->window_id = INVALID_WID;
			t->owns_window = false;
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
			const char* path = reinterpret_cast<const char*>(arg4);
			pt::uint8_t* tp  = reinterpret_cast<pt::uint8_t*>(arg5);
			return (pt::uint64_t)VFS::readdir_ex(path, idx, name, sz, tp);
		}

		case SYS_MEM_FREE:
			return (pt::uint64_t)vmm.memsize();

		case SYS_DISK_SIZE:
			return (pt::uint64_t)VFS::get_total_space();

		case SYS_REMOVE: {
			const char* filename = reinterpret_cast<const char*>(arg1);
			return VFS::delete_file(filename) ? 0 : (pt::uint64_t)-1;
		}

		case SYS_SOCK_CONNECT: {
			pt::uint32_t dst_ip   = (pt::uint32_t)arg1;
			pt::uint16_t dst_port = (pt::uint16_t)arg2;
			Task* t = TaskScheduler::get_current_task();
			int fd = -1;
			for (int i = 3; i < (int)Task::MAX_FDS; i++)
				if (!t->fd_table[i].open) { fd = i; break; }
			if (fd == -1) return (pt::uint64_t)-1;
			TcpSocket* sock = tcp_connect(dst_ip, dst_port, 250);
			if (!sock) return (pt::uint64_t)-1;
			File* f  = &t->fd_table[fd];
			f->open  = true;
			f->type  = FdType::TCP_SOCK;
			tcp_sock_set(f->fs_data, sock);
			sclog("syscall: SYS_SOCK_CONNECT: -> fd %d\n", fd);
			return (pt::uint64_t)fd;
		}

		case SYS_GET_MOUSE_EVENT: {
			MouseEvent ev;
			if (!get_mouse_event(&ev))
				return (pt::uint64_t)-1;
			// Encoding: bits[7:0]=dx, bits[15:8]=dy, bit[16]=left, bit[17]=right
			pt::uint64_t result =
				((pt::uint64_t)(pt::uint8_t)ev.dx)            |
				((pt::uint64_t)(pt::uint8_t)ev.dy      <<  8) |
				((pt::uint64_t)ev.left_button           << 16) |
				((pt::uint64_t)ev.right_button          << 17);
			return result;
		}

		case SYS_GET_MICROS:
			return get_microseconds();

		case SYS_AUDIO_WRITE: {
			if (!AC97::is_present()) return (pt::uint64_t)-1;
			const pt::uint8_t* data = reinterpret_cast<const pt::uint8_t*>(arg1);
			pt::uint32_t bytes = static_cast<pt::uint32_t>(arg2);
			if (!data || bytes == 0) return (pt::uint64_t)-1;

			// Auto-open for backward compatibility (Doom passes rate as arg3)
			if (AC97::owner_task_id < 0) {
				Task* t = TaskScheduler::get_current_task();
				if (!t) return (pt::uint64_t)-1;
				pt::uint32_t rate = static_cast<pt::uint32_t>(arg3);
				if (rate == 0) rate = 48000;
				if (!AC97::open(rate, 2, 0)) return (pt::uint64_t)-1;
				AC97::owner_task_id = (pt::int32_t)t->id;
			}

			return (pt::uint64_t)AC97::queue_pcm(data, bytes);
		}

		case SYS_AUDIO_PLAYING: {
			if (!AC97::is_present()) return (pt::uint64_t)-1;
			AC97::poll_dma();
			if (AC97::owner_task_id >= 0 && !AC97::has_free_slot())
				return 1;
			return 0;
		}

		case SYS_WRITE_SERIAL: {
			const char* buf = reinterpret_cast<const char*>(arg1);
			pt::uint64_t len = arg2;
			for (pt::uint64_t i = 0; i < len; i++)
				debug.print_ch(buf[i]);
			return len;
		}

		case SYS_SET_WINDOW_TITLE: {
			Task* t = TaskScheduler::get_current_task();
			Window* w = WindowManager::get_window((pt::uint32_t)arg1);
			if (!t || !w || w->owner_task_id != t->id) return (pt::uint64_t)-1;
			const char* src = reinterpret_cast<const char*>(arg2);
			int i = 0;
			for (; i < 31 && src[i]; ++i) w->title[i] = src[i];
			w->title[i] = '\0';
			// Chrome (including title) is redrawn by the compositor each frame.
			return 0;
		}

		case SYS_BIND_VTERM: {
			pt::uint32_t vt = (pt::uint32_t)arg1;
			if (vt >= VTERM_COUNT) return (pt::uint64_t)-1;
			Task* t = TaskScheduler::get_current_task();
			if (!t) return (pt::uint64_t)-1;
			t->vterm_id = vt;
			return 0;
		}

		case SYS_GETPID: {
			Task* t = TaskScheduler::get_current_task();
			return t ? (pt::uint64_t)t->id : (pt::uint64_t)-1;
		}

		case SYS_STAT: {
			const char* filename = reinterpret_cast<const char*>(arg1);
			StatResult* buf = reinterpret_cast<StatResult*>(arg2);
			if (!filename || !buf) return (pt::uint64_t)-1;
			return VFS::stat_file(filename, buf) ? 0 : (pt::uint64_t)-1;
		}

		case SYS_MPROTECT: {
			pt::uintptr_t addr = (pt::uintptr_t)arg1;
			pt::size_t len     = (pt::size_t)arg2;
			int prot           = (int)arg3;
			return TaskScheduler::mprotect_pages(addr, len, prot) == 0
			       ? 0 : (pt::uint64_t)-1;
		}

		case SYS_LIST_WINDOWS: {
			auto* buf = reinterpret_cast<WindowManager::WinListEntry*>(arg1);
			pt::uint32_t max_entries = (pt::uint32_t)arg2;
			if (!buf || max_entries == 0) return 0;
			return WindowManager::list_windows(buf, max_entries);
		}

		case SYS_LIST_TASKS: {
			auto* buf = reinterpret_cast<TaskScheduler::TaskListEntry*>(arg1);
			pt::uint32_t max_entries = (pt::uint32_t)arg2;
			return TaskScheduler::list_tasks(buf, max_entries);
		}

		case SYS_GET_MOUSE_POS: {
			pt::uint64_t r = (pt::uint64_t)(pt::uint16_t)mouse.pos_x
			               | ((pt::uint64_t)(pt::uint16_t)mouse.pos_y << 16)
			               | ((pt::uint64_t)(mouse.left_button_pressed ? 1 : 0) << 32)
			               | ((pt::uint64_t)(mouse.right_button_pressed ? 1 : 0) << 33);
			return r;
		}

		case SYS_POLL_START_KEY:
			return consume_start_key() ? 1 : 0;

		case SYS_RESIZE_WINDOW: {
			Task* t = TaskScheduler::get_current_task();
			if (!t || t->window_id == INVALID_WID) return (pt::uint64_t)-1;
			bool ok = WindowManager::resize_window(t->window_id,
			    (pt::uint32_t)arg1, (pt::uint32_t)arg2,
			    (pt::uint32_t)arg3, (pt::uint32_t)arg4);
			return ok ? 0 : (pt::uint64_t)-1;
		}

		case SYS_GET_WINDOW_POS: {
			Task* t = TaskScheduler::get_current_task();
			if (!t || t->window_id == INVALID_WID) return (pt::uint64_t)-1;
			Window* w = WindowManager::get_window(t->window_id);
			if (!w) return (pt::uint64_t)-1;
			return (pt::uint64_t)w->client_ox |
			       ((pt::uint64_t)w->client_oy << 16);
		}

		case SYS_SET_FS_BASE: {
			pt::uint64_t base = arg1;
			// Set FS base via MSR 0xC0000100
			asm volatile("wrmsr" :: "c"(0xC0000100U),
			             "a"((pt::uint32_t)(base & 0xFFFFFFFF)),
			             "d"((pt::uint32_t)(base >> 32)));
			return 0;
		}

		case SYS_MKDIR: {
			const char* path = reinterpret_cast<const char*>(arg1);
			if (!path) return (pt::uint64_t)-1;
			return VFS::create_directory(path) ? 0 : (pt::uint64_t)-1;
		}

		case SYS_OPEN_RW: {
			const char* filename = reinterpret_cast<const char*>(arg1);
			Task* t = TaskScheduler::get_current_task();
			int fd = -1;
			for (int i = 3; i < (int)Task::MAX_FDS; i++) {
				if (!t->fd_table[i].open) { fd = i; break; }
			}
			if (fd == -1) return (pt::uint64_t)-1;
			if (!VFS::open_file_readwrite(filename, &t->fd_table[fd])) {
				sclog("syscall: SYS_OPEN_RW: '%s' not found\n", filename);
				return (pt::uint64_t)-1;
			}
			t->fd_table[fd].type = FdType::FILE;
			sclog("syscall: SYS_OPEN_RW: '%s' -> fd %d\n", filename, fd);
			return (pt::uint64_t)fd;
		}

		case SYS_AUDIO_OPEN: {
			if (!AC97::is_present()) return (pt::uint64_t)-1;
			Task* t = TaskScheduler::get_current_task();
			if (!t) return (pt::uint64_t)-1;
			if (AC97::owner_task_id >= 0 && AC97::owner_task_id != (pt::int32_t)t->id)
				return (pt::uint64_t)-1;
			pt::uint32_t rate     = (pt::uint32_t)arg1;
			pt::uint8_t  channels = (pt::uint8_t)arg2;
			pt::uint8_t  format   = (pt::uint8_t)arg3;
			if (!AC97::open(rate, channels, format))
				return (pt::uint64_t)-1;
			AC97::owner_task_id = (pt::int32_t)t->id;
			sclog("syscall: SYS_AUDIO_OPEN: rate=%d ch=%d by task %d\n", rate, channels, t->id);
			return 0;
		}

		case SYS_AUDIO_CLOSE: {
			Task* t = TaskScheduler::get_current_task();
			if (!t || AC97::owner_task_id != (pt::int32_t)t->id)
				return (pt::uint64_t)-1;
			AC97::close();
			sclog("syscall: SYS_AUDIO_CLOSE: by task %d\n", t->id);
			return 0;
		}

		default:
			sclog("syscall: unknown nr=%llu\n", nr);
			return (pt::uint64_t)-1;
	}
}

ASMCALL pt::uint64_t syscall_handler(pt::uint64_t nr, pt::uint64_t arg1,
                                      pt::uint64_t arg2, pt::uint64_t arg3,
                                      pt::uint64_t arg4, pt::uint64_t arg5)
{
	// Snapshot g_syscall_rsp into the current task BEFORE any blocking
	// operation (e.g. waitpid) can cause another task's SYS_EXIT to
	// overwrite the global with a different task's kernel stack RSP.
	Task* ct = TaskScheduler::get_current_task();
	if (ct) ct->syscall_frame_rsp = g_syscall_rsp;

	pt::uint64_t t0 = 0;
	if (g_perf_recording && ct && nr < NUM_SYSCALLS)
		t0 = get_microseconds();

	pt::uint64_t result = syscall_dispatch(nr, arg1, arg2, arg3, arg4, arg5);

	if (t0) {
		extern SyscallPerfData* g_perf_data;
		if (g_perf_data) {
			pt::uint64_t t1 = get_microseconds();
			if (t1 > t0) {
				g_perf_data[ct->id].counts[nr]++;
				g_perf_data[ct->id].usec[nr] += t1 - t0;
			}
		}
	}

	return result;
}
