org 0x7c00
bits 16

%define ENDL  0x0D, 0x0A

jmp short main
nop

bdb_oem: db "MSWIN4.1"
bdb_bytes_per_sector: dw 512
bdb_sectors_per_cluster: db 1
bdb_reserved_sectors: dw 1
bdb_number_of_fats: db 2
bdb_max_root_dir_entries: dw 224
bdb_total_sectors: dw 2880
bdb_media_descriptor: db 0xF0
bdb_sectors_per_fat: dw 9
bdb_sectors_per_track: dw 18
bdb_number_of_heads: dw 2
bdb_hidden_sectors: dd 0
bdb_total_sectors_large: dd 0
ebr_drive_number: db 0
                  db 0
ebr_signature: db 0x29
ebr_volume_id: db 0x12345678
ebr_volume_label: db "FUHREROS    "
ebr_file_system_type: db "FAT12   "


main:
    mov [ebr_drive_number], dl
    mov ax,1
    mov cl,1
    mov bx, 0x7e00

   
    mov sp, 0x7c00
    mov si, msg_hello
    call print

    ; 4 segments
    ; reserved segment: 1 sector
    ; FAT: 9*2 = 18 sectors
    ; Root Directory:
    ; Data

    mov ax, [bdb_sectors_per_fat]
    mov bl, [bdb_number_of_fats]
    xor bh, bh
    mul bx

    add  ax, [bdb_reserved_sectors]
    push ax

    mov ax, [bdb_max_root_dir_entries]
    shl ax, 5
    xor dx, dx
    div word [bdb_bytes_per_sector] ; (32*num of entries)/bytesper sector

    test dx,dx
    jz rootDirAfter
    inc ax

    hlt
rootDirAfter:
    mov cl, al
    pop ax
    mov dl, [ebr_drive_number]
    mov bx, buffer
    call disk_read

    xor bx,bx
    mov di, buffer

search_kernel:
    mov si, file_kernel_bin
    mov cx, 11
    push di
    REPE CMPSB
    pop di
    JE foundKernel

    add di, 32
    inc bx  
    cmp bx, [bdb_max_root_dir_entries]
    jl search_kernel    
    jmp kernelNotFound

kernelNotFound:
    mov si, msg_kernel_not_found
    call print
    hlt
    jmp halt

foundKernel:
    mov ax,[di+26]
    mov [kernel_cluster], ax
    mov ax, [bdb_reserved_sectors]
    mov bx, buffer
    mov cl, [bdb_sectors_per_fat]
    mov dl, [ebr_drive_number]

    call disk_read

    mov bx, kernel_load_segment
    mov es, bx
    mov bx, kernel_load_offset

loadKernelLoop:
    mov ax, [kernel_cluster]
    add ax, 31
    mov cl, 1
    mov dl, [ebr_drive_number]

    call disk_read
    add bx, [bdb_bytes_per_sector]

    mov ax, [kernel_cluster]
    mov cx, 3
    mul cx
    mov cx, 2
    div cx

    mov si, buffer
    add si, ax
    mov ax, [ds:si]

    or dx,dx
    jz even
    shr ax, 4
    jmp nextClusterAfter
even:
    and ax,0x0FFF

nextClusterAfter:
    cmp ax, 0x0FF8
    jae readFinish
    mov [kernel_cluster], ax
    jmp  loadKernelLoop

readFinish:
    mov dl, [ebr_drive_number]
    mov ax, kernel_load_segment
    mov ds, ax
    mov es, ax
    jmp kernel_load_segment:kernel_load_offset

    hlt
    jmp halt
halt:
    jmp halt

;input: LBA index in ax
;cx [bits 0-5] = sector number
;cx [bits 6-11] = cylinder number
;dh  = head number

lba_to_chs:
    push ax
    push dx
    ; sector: (LBA % sectors_per_track) + 1
    xor dx, dx
    div word [bdb_sectors_per_track]

    inc dx
    mov cx, dx
    xor dx, dx

    ; head: (LBA / sectors_per_track) % number_of_heads

    div word [bdb_number_of_heads]
    mov dh, dl


    ; cylinder: (LBA/sectors per track)/ number of heads
    mov ch, al
    shl ah, 6
    or cl,ah

    pop ax
    mov dl,al
    pop ax
    ret

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

disk_read:
    push ax
    push bx
    push cx
    push dx
    push di

    call lba_to_chs

    mov ah, 0x2
    mov di, 3 ; counter

    jmp retry
retry:
    stc
    int 0x13
    jnc doneRead
    call diskReset

    dec di
    test di, di
    jnz retry

failDiskRead:
    mov si, read_failure
    call print
    hlt
    jmp halt


doneRead:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax

diskReset:
    pusha
    mov ah,0
    stc
    int 13h
    jc failDiskRead
    popa
    ret



msg_hello: db "Hello, World!", ENDL, 0
read_failure:  db "Failed to read disk", ENDL,0
file_kernel_bin db 'KERNEL  BIN'
msg_kernel_not_found db 'KERNEL.BIN not found'
kernel_cluster dw 0
kernel_load_segment equ 0x200
kernel_load_offset equ 0

times 510 - ($ - $$) db 0
dw 0AA55h

buffer:
