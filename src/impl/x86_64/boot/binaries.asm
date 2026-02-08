%macro IncBin 2
    SECTION .rodata
    GLOBAL %1
%1:
    incbin %2
    db 0
    %1_size: dq %1_size - %1
%endmacro

; Logo and PotatoLogo are now loaded from FAT12 disk
; IncBin Logo, "src/impl/x86_64/bins/potato.txt"
; IncBin PotatoLogo, "src/impl/x86_64/bins/potato.raw"