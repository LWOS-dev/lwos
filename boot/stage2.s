
[bits 16]
section .stage2.entry
global stage2_entry

VBE_INFO_BLOCK      equ 0x9100
FAT_SCRATCH         equ 0x9100

LOADER_SEG          equ 0x1000 ; -> 0x10000

BINFO_BASE      equ 0x9000
BINFO_MAGIC     equ 0x4F464E42          ; 'BNFO'
BINFO_SIZE      equ 0xE4
BI_MAGIC        equ BINFO_BASE + 0x00
BI_SIZE         equ BINFO_BASE + 0x04
BI_FLAGS        equ BINFO_BASE + 0x08
BI_VBE_MODE     equ BINFO_BASE + 0x0C
BI_FB_PHYS      equ BINFO_BASE + 0x10
BI_FB_PITCH     equ BINFO_BASE + 0x14
BI_FB_W         equ BINFO_BASE + 0x18
BI_FB_H         equ BINFO_BASE + 0x1A
BI_FB_BPP       equ BINFO_BASE + 0x1C
BI_FB_RED       equ BINFO_BASE + 0x1D   ; 6 mask size/position bytes to 0x22
BI_EDID         equ BINFO_BASE + 0x60
BI_VBE_VER      equ BINFO_BASE + 0xE0
BI_VBE_MEM      equ BINFO_BASE + 0xE2

BINFO_F_VBE     equ 0x01
BINFO_F_LFB     equ 0x02
BINFO_F_EDID    equ 0x04
BINFO_F_MODESET equ 0x20                ; stays clear here: we only collect

VBE_SP        equ 0x90E4        ; word, caller's sp while the ROM has the stack
VBE_BEST      equ 0x90E8        ; dword, packed score of the best mode so far
VBE_PRIO      equ 0x90EC        ; byte, bpp preference of the mode being scored
VIB           equ 0x9100        ; VbeInfoBlock, 512 bytes
MIB           equ 0x9300        ; ModeInfoBlock, 256 bytes
MODELIST      equ 0x9400        ; our own copy of the mode list, 256 words
VBE_STACK_TOP equ 0xA000        ; 2.5KB from 0x9600, for the ROM to run on

stage2_entry:

    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x900
    mov [BOOT_DRIVE], dl
    ; BIOS 磁盘调用期间恢复启动时的 IRQ 屏蔽状态。
    in al, 0x21
    mov [TB_BIOS_PIC], al
    in al, 0xa1
    mov [TB_BIOS_PIC+1], al

    call loader_load
    call a20_enable
    call e820_collecct
    call vbs_harvest

    mov si, str_suc
    call print16

    lgdt [gdt_desc]

    cli

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp 0x8:pm_entry

; A20
a20_enable:
    call a20_test
    jc .done

    mov ax, 0x2401
    int 0x15
    call a20_test
    jc .done

    call kbc_wait_in
    mov al, 0xd1
    out 0x64, al
    call kbc_wait_in
    mov al, 0xdf
    out 0x60, al
    call kbc_wait_in
    call a20_test
    jc .done

    in al, 0x92
    test al, 2
    jnz .fail
    or al, 2
    and al, 0xfe
    out 0x92, al
    call a20_test
    jc .done
.fail:
    mov si, str_a20
    call print16
    cli
.hang:
    hlt
    jmp .hang
.done:
    ret

a20_test:
    push ax
    push bx
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov ax, 0xffff
    mov es, ax
    mov bl, [0x0500]
    mov bh, [es:0x0510]
    mov byte [0x0500], 0
    mov byte [es:0x0510], 0xff
    mov al, [0x0500]
    mov [0x0500], bl
    mov [es:0x0510], bh
    cmp al, 0xff
    je .off
    stc
    jmp .out
.off:
    clc
.out:
    pop es
    pop ds
    pop bx
    pop ax
    ret

kbc_wait_in:
    push ax
    push cx
    xor cx, cx
.L1:
    in al, 0x64
    test al, 2
    jz .ok
    loop .L1
.ok:
    pop cx
    pop ax
    ret

MMAP_BASE       equ 0x8000
MMAP_COUNT      equ 0x8004
MMAP_FLAGS      equ 0x8008
MMAP_ENTRIES    equ 0x8010
MMAP_MAX        equ 128
MMAP_F_E801     equ 1

; E820
e820_collecct:
    pushad
    xor eax, eax
    mov [MMAP_BASE], eax
    mov [MMAP_COUNT], eax
    mov [MMAP_FLAGS], eax

    mov di,MMAP_ENTRIES
    xor ebx, ebx
    xor bp, bp
.next:
    mov dword [es:di+20], 1

    mov eax, 0xe820
    mov edx, 0x534d4150 ; 'SMAP'
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534d4150
    jne .done
    jcxz .skip
    test byte [es:di+20], 1
    jz .skip

    mov eax, [es:di+8]
    or eax, [es:di+12]
    jz .skip
    inc bp
    add di, 20
    cmp bp, MMAP_MAX
    jae .done
.skip:
    test ebx, ebx
    jnz .next
.done:
    test bp, bp
    jz .fallback

    mov [MMAP_COUNT], bp
    mov dword [MMAP_BASE], 0x53445241
    popad
    ret
; no e820, use e801
.fallback:
    xor ax, ax
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov ax, 0xe801
    int 0x15
    jc .none

    ; mostly using ax, bx
    ; if ax,bx =0, cx,dx might be non zero
    test ax, ax
    jnz .use_axbx
    test bx, bx
    jnz .use_axbx
    ; try cx,dx
    mov ax, cx
    mov bx, dx
.use_axbx:
    ; AX = 1MB~16MB CNT(KB), BX = 16MB~4GB CNT(64KB)
    test ax, ax
    jnz .have_low
    test bx, bx
    jz .none            ; really no mem here

.have_low:
    mov di, MMAP_ENTRIES

    ; ---- I BASE 0 ~ 0x9FC00 ----
    xor eax, eax
    mov [es:di], eax
    mov [es:di+4], eax
    mov dword [es:di+8], 0x0009fc00
    mov [es:di+12], eax
    mov dword [es:di+16], 1
    add di, 20

    ; ---- II 1MB+ ，LENGTH = AX KB ----
    test ax, ax
    jz .no_low            ; 1MB~16MB=0, skip
    mov dword [es:di], 0x00100000
    mov [es:di+4], eax    ; hi32 base = 0
    movzx edx, ax
    shl edx, 10           ; KB->bytes
    mov [es:di+8], edx
    mov dword [es:di+12], 0
    mov dword [es:di+16], 1
    add di, 20
.no_low:

    ; ---- III 16MB+，LENGTH = BX * 64KB ----
    test bx, bx
    jz .no_high
    mov dword [es:di], 0x01000000   ; 16MB
    mov dword [es:di+4], 0
    movzx edx, bx
    shl edx, 16           ; 64KB->bytes
    mov [es:di+8], edx
    mov dword [es:di+12], 0
    mov dword [es:di+16], 1
    add di, 20
.no_high:

    ; actuall count entries
    mov ax, di
    sub ax, MMAP_ENTRIES
    xor dx, dx
    mov cx, 20
    div cx                ; AX = count
    mov [MMAP_COUNT], ax

    mov dword [MMAP_FLAGS], MMAP_F_E801
    mov dword [MMAP_BASE], 0x53445241
.none:
    popad
    ret

vbs_harvest:
    pushad
    mov [VBE_SP], sp
    mov sp, VBE_STACK_TOP

    xor ax, ax
    mov es, ax
    mov fs, ax
    cld

    mov edi, BINFO_BASE
    mov ecx, BINFO_SIZE / 4
    xor eax, eax
    rep stosd
    mov dword [BI_SIZE], BINFO_SIZE
    mov dword [VBE_BEST], 0

    ; 4f00

    mov di, VIB
    mov dword [di], 0x32454256
    mov ax, 0x4f00
    int 0x10
    call vbe_fixseg
    cmp ax, 0x004f
    jne .done
    cmp dword [VIB], 0x41534556
    jne .done

    mov ax, [VIB+4] ; version
    mov [BI_VBE_VER], ax
    mov ax, [VIB+18] ; video
    mov [BI_VBE_MEM], ax
    or dword [BI_FLAGS], BINFO_F_VBE

    ; mode list

    mov si, [VIB+14]
    mov ax, [VIB+16]
    mov fs, ax
    mov di, MODELIST
    xor cx, cx
.copy:
    mov ax, [fs:si]
    add si, 2
    mov [di], ax
    add di, 2
    cmp ax, 0xffff
    je .walk
    inc cx
    cmp cx, 255
    jb .copy
    mov word [di], 0xffff
.walk:
    mov si, MODELIST

    ; 4f01
.next:
    mov cx, [si]
    add si, 2
    cmp cx, 0xffff
    je .edid

    mov di, MIB
    mov ax, 0x4f01
    int 0x10
    call vbe_fixseg
    cmp ax, 0x004f
    jne .next

    mov ax, [MIB]
    and ax, 0x99
    cmp ax, 0x99
    jne .next
    cmp byte [MIB+0x1b], 6
    jne .next

    mov al, [MIB+0x19]
    cmp al, 32
    je .p32
    cmp al, 24
    je .p24
    jmp .next
.p32:
    mov byte [VBE_PRIO], 2
    jmp .score
.p24:
    mov byte [VBE_PRIO], 1
.score:
    mov ax, [MIB+0x12]
    cmp ax, 1920
    ja .next
    mov bx, [MIB+0x14]
    cmp bx, 1200
    ja .next

    movzx eax, ax
    movzx ebx, bx
    mul ebx
    movzx edx, byte [VBE_PRIO]
    shl edx, 28
    or eax, edx
    cmp eax, [VBE_BEST]
    jbe .next
    mov [VBE_BEST], eax
    call vbe_store
    jmp .next

.edid:
    ; 4f15
    mov ax, 0x4f15
    mov bl, 1
    xor cx, cx
    xor dx, dx
    mov di, BI_EDID
    int 0x10
    call vbe_fixseg
    cmp ax, 0x004f
    jne .wipe
    cmp dword [BI_EDID], 0xffffff00
    jne .wipe
    cmp dword [BI_EDID+4], 0x00ffffff
    jne .wipe
    or dword [BI_FLAGS], BINFO_F_EDID
    jmp .done
.wipe:
    mov di, BI_EDID
    mov ecx, 32
    xor eax, eax
    rep stosd
.done:
    mov dword [BI_MAGIC], BINFO_MAGIC
    mov sp, [VBE_SP]
    popad
    ret

; copy modes into block, cx = mode num
vbe_store:
    push si
    push di
    push cx
    mov [BI_VBE_MODE], cx
    mov eax, [MIB+0x28]
    mov [BI_FB_PHYS], eax
    or dword [BI_FLAGS], BINFO_F_LFB

    mov eax, 00
    cmp word [BI_VBE_VER], 0x0300
    jb .old
    movzx eax, word [MIB+0x32]
    test eax, eax
    jnz .havepitch
.old:
    movzx eax, word [MIB+0x10]
.havepitch:
    mov [BI_FB_PITCH], eax
    mov ax, [MIB+0x12]
    mov [BI_FB_W], ax ; width
    mov ax, [MIB+0x14]
    mov [BI_FB_H], ax ; height
    mov al, [MIB+0x19]
    mov [BI_FB_BPP], al ; bpp

    mov si, MIB+0x1f
    cmp word [BI_VBE_VER], 0x0300
    jb .masks
    mov si, MIB+0x36
.masks:
    mov di, BI_FB_RED
    mov cx, 6
    rep movsb

    pop cx
    pop di
    pop si

; some function may change the ds, es
; or the dir flag, zero them
vbe_fixseg:
    push ax
    xor ax, ax
    mov dx, ax
    mov es, ax
    cld 
    pop ax
    ret

; =================================
; detailed dap buffer structures
;    +0h  BYTE  length of the buffer = 10h
;    +1h  BYTE  reserved
;    +2h  WORD  sectors count
;    +4h  WORD  buffer address 0000xxxxh
;    +6h  WORD  buffer segment xxxx0000h
;    +8h QWORD  start lba

; =================================
; detailed fat32 structures
;    +0h    3B  jump instructions
;    +3h    8B  OEM
;    +bh   80B  BPB and extended BPB
;   +5bh  419B  codes
;  +1feh  WORD  0xaa55
; ---------------------------------
; BPB inside
;    +bh  WORD  bytes per sector
;    +dh  BYTE  sectors per cluster
;    +eh  WORD  sectors count from boot to 1st FAT
;               defaultly 32(20h)
;    

loader_load:
    mov byte [dap_buffer], 0x10
    mov byte [dap_buffer + 1], 0
    mov word [dap_buffer + 2], 1              ; 1 s
    mov word [dap_buffer + 4], FAT_SCRATCH    ; to fat scr
    mov word [dap_buffer + 6], 0              ; seg0
    mov dword [dap_buffer + 8], PART_LBA      ; lba start
    mov dword [dap_buffer + 12], 0            ; 0
    call read_sector
    jc .ioerr

    cmp word [FAT_SCRATCH + 0x1fe], 0xaa55
    push dword 1
    jne .nofs
    add esp, 4

    cmp byte [FAT_SCRATCH + 0x0D], 1
    push dword 2
    jne .nofs
    add esp, 4

    ; 
    movzx eax, word [FAT_SCRATCH + 0x0e] ; word -> dword
    add eax, PART_LBA
    mov [FAT_START_LBA], eax

    movzx ecx, byte [FAT_SCRATCH + 0x10] ; N_FATS
    mov edx, [FAT_SCRATCH + 0x24]        ; FAT size in sectors
    imul edx, ecx
    add eax, edx ; + nfats*fatsz=data area start
    mov [DATA_START_LBA], eax

    mov eax, [FAT_SCRATCH + 0x2c]        ; root cluster, normally 2 
    mov [cur_cluster], eax

    ; walk through the root directory looking for ldr.bin
.dir_next_cluster:
    mov eax, [cur_cluster]
    call cluster_to_lba
    ; modify the dap buf's START LBA value
    mov [dap_buffer+8], eax
    ;                      BUFFER ADDR value
    mov word [dap_buffer+4], FAT_SCRATCH
    call read_sector
    jc .ioerr

    mov di, FAT_SCRATCH
    mov cx, 16

.dir_scan:
    cmp byte [di], 0
    push dword 3 ; no media
    je .nofs
    add esp, 4
    push di
    push cx
    mov si, LOADER_BIN_NAME
    mov cx, 11
    repe cmpsb
    pop cx
    pop di
    je .found
    add di, 32
    loop .dir_scan

    mov eax, [cur_cluster]
    call get_next_cluster
    push dword 4
    jc .nofs
    add esp, 4
    mov [cur_cluster], eax
    jmp .dir_next_cluster
.found:
    ; here DI still point @ the matched entry's
    ;  start
    movzx eax, word [di+0x14] ; 0xffff0000 masked
    shl eax, 16
    movzx edx, word [di+0x1a] ; 0x0000ffff masked
    or eax, edx
    mov [cur_cluster], eax
    mov si, LOADER_SEG
.file_next_cluster:
    mov eax, [cur_cluster]
    cmp eax, 0x0ffffff8 ; end of file
    jae .done
    call cluster_to_lba
    mov [dap_buffer+8], eax
    mov word [dap_buffer+4], 0
    mov word [dap_buffer+6], si
    call read_sector
    jc .ioerr

    add si, 0x20 ; equal to add 200h
    mov eax, [cur_cluster]
    call get_next_cluster
    jc .done
    mov [cur_cluster], eax
    jmp .file_next_cluster
.done:
    ret
.ioerr:
    mov si, str_ioerr
    call print16
    jmp .halted
.nofs:
    pop eax
    add al, 0x30
    mov [str_nofs], al
    mov si, str_nofs
    call print16
    jmp .halted
.halted:
    hlt
    jmp .halted

; data_start + (cluster - 2)
cluster_to_lba:
    sub eax, 2
    add eax, [DATA_START_LBA]
    ret

get_next_cluster:
    mov ecx, eax
    shl ecx, 2
    mov edx, ecx
    shr edx, 9
    and cx, 0x1ff
    add edx, [FAT_START_LBA]
    mov [dap_buffer+8], edx
    mov word [dap_buffer+4], FAT_SCRATCH
    mov word [dap_buffer+6], 0
    call read_sector
    jc .eoc
    mov bx, FAT_SCRATCH
    add bx, cx
    mov eax, [bx]
    and eax, 0x0fffffff
    cmp eax, 0x0ffffff8
    jae .eoc
    clc
    ret
.eoc:
    stc
    ret

read_sector:
    mov bx, 3
.retry:
    pushad
    mov si, dap_buffer
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    popad
    jnc .ok
    dec bx
    jz .fail
    push ax
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13
    pop ax
    jmp .retry
.ok:
    clc
    ret
.fail:
    stc
    ret

print16:
.L1:
    mov al, [si]
    inc si
    test al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp .L1
.done:
    ret

LOADER_BIN_NAME: db "LOADER  BIN"
FAT_START_LBA   equ 0x8A10
DATA_START_LBA  equ 0x8A14
cur_cluster     equ 0x8A18
str_ioerr: db "IO ERROR", 10, 13, 0
str_nofs: db "? NO FILESYSTEM FOUND", 10, 13, 0
str_a20: db "FAILED INIT A20", 10, 13, 0
str_suc: db "DONE", 10, 13, 0
BOOT_DRIVE equ 0x90ee
dap_buffer equ 0x90f0

; gdt

align 8
gdt_start:
    dq 0 ; NUL
gdt_code:
    dq 0x00cf9a000000ffff
    ; base=0 limit=0xfffff g=1
    ; d/b=1 p=1 dpl=0 code r/x
gdt_data:
    dq 0x00cf92000000ffff
    ; base=0 limit=0xfffff g=1
    ; d/b=1 p=1 dpl=0 data r/w
gdt_code16:
	dq 0x00009a000000ffff
gdt_data16:
	dq 0x000092000000ffff
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

idt_real:
	dw 0x3FF
	dd 0

CODE_SEL        equ gdt_code - gdt_start
DATA_SEL        equ gdt_data - gdt_start

TRAMP_BLK       equ 0xA000
TB_ESP          equ TRAMP_BLK + 0x00    ; dword, 32-bit esp across the trip
TB_PIC          equ TRAMP_BLK + 0x04    ; 2 bytes, master then slave IRQ mask
TB_GDTR         equ TRAMP_BLK + 0x08    ; 6 bytes, sgdt
TB_IDTR         equ TRAMP_BLK + 0x10    ; 6 bytes, sidt
; Register block: caller fills it, the ROM's answers come back in it.
TRAMP_REGS      equ TRAMP_BLK + 0x18
TB_AX           equ TRAMP_REGS + 0x00
TB_BX           equ TRAMP_REGS + 0x02
TB_CX           equ TRAMP_REGS + 0x04
TB_DX           equ TRAMP_REGS + 0x06
TB_ES           equ TRAMP_REGS + 0x08
TB_DI           equ TRAMP_REGS + 0x0A
TB_BP           equ TRAMP_REGS + 0x0C
TB_FLAGS        equ TRAMP_REGS + 0x0E
TB_VEC          equ TRAMP_BLK + 0x28    ; dword, &tramp_int10 for ABI.BIN to call
TB_KIND         equ TRAMP_BLK + 0x2C    ; byte: 0 video, 1 boot disk
TB_DISK_VEC     equ TRAMP_BLK + 0x30    ; dword, &tramp_disk_read for loader
TB_BIOS_PIC     equ TRAMP_BLK + 0x34    ; 2 bytes, original BIOS IRQ masks

TRAMP_STACK_TOP equ 0xc000

[bits 32]
pm_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0xa0000

    mov dword [TB_VEC], tramp_int10
    mov dword [TB_DISK_VEC], tramp_disk_read

    jmp 0x10000

[bits 32]
global tramp_int10
tramp_int10:
    pushfd
    pushad
    cli
    mov byte [TB_KIND], 0
    jmp tramp_enter

; 固定调用 INT 13h/AH=42h: loader 填好 0x90F0 的 DAP。
; 仅供启动阶段使用, 原生驱动接管磁盘/PIC 后不能再调用。
global tramp_disk_read
tramp_disk_read:
    pushfd
    pushad
    cli
    mov byte [TB_KIND], 1

tramp_enter:

    sgdt [TB_GDTR]
    sidt [TB_IDTR]

    in al, 0x21
    mov [TB_PIC], al
    in al, 0xa1
    mov [TB_PIC+1], al
    mov al, 0xff
    out 0x21, al
    out 0xa1, al

    mov [TB_ESP], esp
    jmp CODE16_SEL:.step16
[bits 16]
.step16:
    mov ax, DATA16_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    lidt [idt_real]

    mov eax, cr0
    and eax, 0xfffffffe
    mov cr0, eax

    db 0xea
    dw .rm_go
    dw 0
.rm_go:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, TRAMP_STACK_TOP

    cmp byte [TB_KIND], 1
    je .disk

    mov ax, [TB_ES]
    push ax
    mov ax, [TB_AX]
    mov cx, [TB_CX]
    mov dx, [TB_DX]
    mov bx, [TB_BX]
    mov di, [TB_DI]
    mov bp, [TB_BP]
    pop es

    int 0x10
    jmp .save_flags

.disk:
    ; INT 13h 可能等待硬件 IRQ, 不沿用视频调用的全屏蔽状态。
    mov al, [TB_BIOS_PIC]
    out 0x21, al
    mov al, [TB_BIOS_PIC+1]
    out 0xa1, al
    mov si, dap_buffer             ; DS=0, DS:SI 指向 DAP
    mov dl, [BOOT_DRIVE]           ; BIOS 启动盘号, 不假定为 80h
    mov ax, 0x4200
    cld
    sti
    int 0x13

.save_flags:
    ; 必须在任何会修改 CF 的指令前保存 BIOS 返回状态。
    pushf
    pop word [cs:TB_FLAGS]
    cli

    mov [cs:TB_AX], ax
    mov [cs:TB_CX], cx
    mov [cs:TB_DX], dx
    mov [cs:TB_BX], bx
    mov [cs:TB_DI], di
    mov [cs:TB_BP], bp
    mov ax, es
    mov [cs:TB_ES], ax

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    call a20_test
    jc .a20ok
    call a20_enable
.a20ok:
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    db 0x66
    db 0xea
    dd .pm_back
    dw CODE_SEL

[bits 32]
.pm_back:
    mov ax, DATA_SEL
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov esp, [TB_ESP]

    lgdt [TB_GDTR]
    lidt [TB_IDTR]

    mov al, [TB_PIC]
    out 0x21, al
    mov al, [TB_PIC+1]
    out 0xa1, al

    popad
    popfd
    ret

CODE16_SEL equ gdt_code16 - gdt_start
DATA16_SEL equ gdt_data16 - gdt_start
