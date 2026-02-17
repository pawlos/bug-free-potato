kernel_source_files := $(shell find src/impl/kernel -name *.cpp)
kernel_object_files := $(patsubst src/impl/kernel/%.cpp, build/kernel/%.o, $(kernel_source_files))

x86_64_cpp_source_files := $(shell find src/impl/x86_64 -name *.cpp)
x86_64_cpp_object_files := $(patsubst src/impl/x86_64/%.cpp, build/x86_64/%.o, $(x86_64_cpp_source_files))

x86_64_asm_source_files := $(shell find src/impl/x86_64 -name *.asm)
x86_64_asm_object_files := $(patsubst src/impl/x86_64/%.asm, build/x86_64/%.o, $(x86_64_asm_source_files))

CPP=g++
CPPFLAGS=-DKERNEL_LOG
QEMU=/mnt/c/Program\ Files/qemu/qemu-system-x86_64.exe
NASM=nasm
LD=ld
# Audio: emulate an AC97 card.
# On Windows hosts use dsound; on Linux use pa (PulseAudio) or alsa.
QEMU_AUDIO_BACKEND ?= dsound
QEMU_OPTIONS=-m 512M \
	-audiodev $(QEMU_AUDIO_BACKEND),id=audio0 \
	-device AC97,audiodev=audio0

x86_64_object_files := $(x86_64_cpp_object_files) $(x86_64_asm_object_files)

$(kernel_object_files): build/kernel/%.o : src/impl/kernel/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/intf -I src/impl/x86_64 -g -masm=intel -ffreestanding -Wall -Wextra $(CPPFLAGS) $(patsubst build/kernel/%.o, src/impl/kernel/%.cpp, $@) -o $@

$(x86_64_cpp_object_files): build/x86_64/%.o : src/impl/x86_64/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/intf -g -masm=intel -ffreestanding -Wall -Wextra $(CPPFLAGS) $(patsubst build/x86_64/%.o, src/impl/x86_64/%.cpp, $@) -o $@


$(x86_64_asm_object_files): build/x86_64/%.o : src/impl/x86_64/%.asm
	mkdir -p $(dir $@) && \
	$(NASM) -f elf64 $(patsubst build/x86_64/%.o, src/impl/x86_64/%.asm, $@) -o $@

.PHONY: all
all: build-cd
build-cd: build-x86_64
	cp dist/x86_64/kernel.bin target/x86_64/iso/boot/kernel.bin && \
	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/kernel.iso target/x86_64/iso

build-x86_64: $(kernel_object_files) $(x86_64_object_files)
	mkdir -p dist/x86_64 && \
	$(LD) -n -o dist/x86_64/kernel.bin -T target/x86_64/linker.ld $(kernel_object_files) $(x86_64_object_files)

gdb: all disk.img
	$(QEMU) -cdrom dist/x86_64/kernel.iso -drive file=disk.img,format=raw,if=ide,media=disk -boot order=d -serial stdio $(QEMU_OPTIONS) -s -S

clean:
	-rm -f build/kernel/*.o
	-rm -f build/x86_64/*.o
	-rm -f build/x86_64/boot/*.o
	-rm -f build/x86_64/device/*.o
	-rm -f build/x86_64/filesystem/*.o
	-rm -f dist/x86_64/kernel.*
	-rm -f disk.img

# Files to copy to disk image from bins folder
BIN_FILES := $(wildcard src/impl/x86_64/bins/*)

# Create FAT12 disk image with test files from bins folder
disk.img: $(BIN_FILES)
	@echo "Creating FAT12 disk image..."
	@# Create 10MB disk image (large enough for testing)
	dd if=/dev/zero of=disk.img bs=512 count=20480 2>/dev/null
	@# Format as FAT12 (1.44MB floppy geometry)
	mkfs.vfat -F 12 -n "POTATDISK" disk.img
	@# Copy all files from bins folder
	@for file in $(BIN_FILES); do \
		filename=$$(basename $$file); \
		uppername=$$(echo $$filename | tr '[:lower:]' '[:upper:]'); \
		echo "  Copying $$filename -> $$uppername"; \
		mcopy -i disk.img $$file ::$$uppername; \
	done
	@echo "Disk image created with files:"
	@mdir -i disk.img ::

run: all disk.img
	$(QEMU) -cdrom dist/x86_64/kernel.iso -drive file=disk.img,format=raw,if=ide,media=disk -boot order=d -serial stdio $(QEMU_OPTIONS)

rundisk: build-x86_64 disk.img
	$(QEMU) -kernel dist/x86_64/kernel.bin -drive file=disk.img,format=raw,if=ide,media=disk -serial stdio $(QEMU_OPTIONS)
