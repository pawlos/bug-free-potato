%macro IncBin 2
    SECTION .rodata
    GLOBAL %1
%1:
    incbin %2
    db 0
    %1_size: dq %1_size - %1
%endmacro

IncBin Logo, "src/impl/x86_64/bins/potato.txt"
IncBin PotatoLogo, "src/impl/x86_64/bins/potato.raw"