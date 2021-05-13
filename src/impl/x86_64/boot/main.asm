global start

section .text
bits 32
start:
	; print 'potato'
	mov dword [0xb8000], 0x2f6f2f70
	mov dword [0xb8004], 0x2f612f74
	mov dword [0xb8008], 0x2f6f2f74

	hlt