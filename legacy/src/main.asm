org 0x7c00
bits 16

%define ENDL  0x0D, 0x0A

main:
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov sp, 0x7c00
    mov si, msg_hello
    call print
    hlt


halt:
    jmp halt

print:
    push si
    push ax
    push bx
    

print_loop:
    lodsb 
    or al, al
    jz print_done

    mov ah, 0x0E
    mov bh, 0
    int 0x10

    jmp print_loop

print_done:
    pop bx
    pop ax
    pop si
    ret
    

msg_hello: db "OUR OS has booted!", ENDL, 0
times 510 - ($ - $$) db 0
dw 0AA55h