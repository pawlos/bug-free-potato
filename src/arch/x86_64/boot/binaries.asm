%macro IncBin 2
    SECTION .rodata
    GLOBAL %1
%1:
    incbin %2
    db 0
    %1_size: dq %1_size - %1
%endmacro

; Logo and PotatoLogo are now loaded from FAT12 disk
; IncBin Logo, "assets/potato.txt"
; IncBin PotatoLogo, "assets/potato.raw"