section .data
    pathname dd "/mnt/c/Users/ASUS/Desktop/SEM7/tat.txt", 0
    towrite db "Hello, World!", 0AH,0DH,"$"
section .bss
    buffer resb 1024
section .text

global main

main:
    mov eax , 5          ; syscall number for sys_open
    mov ebx , pathname   ; pointer to the pathname
    mov ecx , 101o          ; flags (O_RDONLY)
    mov edx , 700o           ; mode (not used for O_RDONLY)
    int 0x80
    
    mov ebx,eax
    mov eax, 4          ; syscall number for sys_read
    mov ecx, towrite     ; pointer to the buffer
    mov edx, 15       ; number of bytes to read
    int 0x80

    mov eax, 1          ; syscall number for sys_exit
    int 0x80