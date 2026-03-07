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
				Task* wt = TaskScheduler::get_current_task();
				bool has_window = wt && wt->window_id != INVALID_WID;
				bool has_vterm  = wt && wt->vterm_id  != INVALID_VT;
				for (pt::uint32_t i = 0; i < n; i++) {
					if (has_window) {
						WindowManager::put_char(wt->window_id, buf[i]);
					} else if (has_vterm) {
						vterm_get(wt->vterm_id)->put_char(buf[i]);
					} else {
						// Unbound tasks → active VTerm (visible console)
						vterm_active()->put_char(buf[i]);
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
			klog("syscall: SYS_CLOSE: fd %d\n", fd);
			return 0;
		}
		case SYS_MMAP: {
			Task* ct = TaskScheduler::get_current_task();
			pt::size_t size = ((pt::size_t)arg1 + 4095) & ~(pt::size_t)4095;
			if (!ct || size == 0) return (pt::uint64_t)-1;
			pt::uintptr_t va = ct->user_heap_top;
			TaskScheduler::map_user_pages(ct, va, size);
			ct->user_heap_top += size;
			klog("syscall: SYS_MMAP size=%d -> va=%lx\n", (int)size, va);
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
				pt::uint64_t* pt = (pt::uint64_t*)(KERNEL_OFFSET + (upd[pd_idx] & ~(pt::uintptr_t)0xFFF));
				if (pt[pt_idx] & 0x01) {
					vmm.free_frame(pt[pt_idx] & ~(pt::uintptr_t)0xFFF);
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
					if (!WindowManager::is_on_active_vt(t->window_id))
						return 0;
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
					if (!WindowManager::is_on_active_vt(t->window_id))
						return 0;
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
					if (!WindowManager::is_on_active_vt(t->window_id))
						return 0;
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
			klog("syscall: SYS_SOCK_CONNECT: -> fd %d\n", fd);
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
			if (AC97::is_playing())  return 0;  /* busy — caller should retry */
			const pt::uint8_t* data = reinterpret_cast<const pt::uint8_t*>(arg1);
			pt::uint32_t bytes = static_cast<pt::uint32_t>(arg2);
			pt::uint32_t rate  = static_cast<pt::uint32_t>(arg3);
			return AC97::play_pcm(data, bytes, rate) ? 1 : (pt::uint64_t)-1;
		}

		case SYS_AUDIO_PLAYING:
			if (!AC97::is_present()) return (pt::uint64_t)-1;
			return AC97::is_playing() ? 1 : 0;

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
			WindowManager::draw_chrome(w->id, WindowManager::is_focused(w->id));
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

		default:
			klog("syscall: unknown nr=%llu\n", nr);
			return (pt::uint64_t)-1;
	}
}
