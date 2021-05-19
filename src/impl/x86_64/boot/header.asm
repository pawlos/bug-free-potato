section .multiboot_header
header_start:
	dd 0xe85250d6					; multiboot
	dd 0							; protected mode i386
	dd header_end - header_start
	dd -(0xe85250d6 + 0 + (header_end - header_start))
	
	dw 0
	dw 0
	dd 8
header_end: