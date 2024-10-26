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
QEMU_OPTIONS=-m 512M -audio driver=sdl,model=ac97

x86_64_object_files := $(x86_64_cpp_object_files) $(x86_64_asm_object_files)

$(kernel_object_files): build/kernel/%.o : src/impl/kernel/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/intf -I src/impl/x86_64 -g -masm=intel -ffreestanding $(CPPFLAGS) $(patsubst build/kernel/%.o, src/impl/kernel/%.cpp, $@) -o $@

$(x86_64_cpp_object_files): build/x86_64/%.o : src/impl/x86_64/%.cpp
	mkdir -p $(dir $@) && \
	$(CPP) -c -I src/intf -g -masm=intel -ffreestanding $(CPPFLAGS) $(patsubst build/x86_64/%.o, src/impl/x86_64/%.cpp, $@) -o $@


$(x86_64_asm_object_files): build/x86_64/%.o : src/impl/x86_64/%.asm
	mkdir -p $(dir $@) && \
	$(NASM) -f elf64 $(patsubst build/x86_64/%.o, src/impl/x86_64/%.asm, $@) -o $@

.PHONY: all
all: build-cd
build-cd: build-x86_64
	cp dist/x86_64/kernel.bin target/x86_64/iso/boot/kernel.bin && \
	grub-mkrescue /usr/lib/grub/i386-pc -o dist/x86_64/kernel.iso target/x86_64/iso

build-x86_64: clean $(kernel_object_files) $(x86_64_object_files)
	mkdir -p dist/x86_64 && \
	$(LD) -n -o dist/x86_64/kernel.bin -T target/x86_64/linker.ld $(kernel_object_files) $(x86_64_object_files)

gdb: all
	$(QEMU) -cdrom dist/x86_64/kernel.iso -serial stdio $(QEMU_OPTIONS) -s

clean:
	-rm -f build/kernel/*.o
	-rm -f build/x86_64/*.o
	-rm -f build/x86_64/boot/*.o
	-rm -f build/x86_64/device/*.o
	-rm -f build/x86_64/libs/*.o
	-rm -f dist/x86_64/kernel.*

run: all
	$(QEMU) -cdrom dist/x86_64/kernel.iso -serial stdio $(QEMU_OPTIONS)
