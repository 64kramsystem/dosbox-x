; SPDX-License-Identifier: GPL-2.0-or-later
; Harmless real-mode LOCK tests. The host checks each reported register/fault result.
bits 16
org 100h

%define UD          2
%define SP_BEFORE   4
%define SP_AFTER    6
%define STACK_TOP   8
%define UD_IP       10
%define DB          12
%define DB_FIRST    14
%define DB_LAST     16
%define MEMORY      18
%define OP_IP       20
%define END_IP      22
%define UD_CS       24
%define TEST_CS     26
%define AX_AFTER    28
%define CX_AFTER    30
%define FLAGS_AFTER 32
%define MEMORY_HI   34
%define REC_SIZE    52
%define MEM (variable - $$ + 100h) & 0ffh, (variable - $$ + 100h) >> 8
%assign case_count 0

section .data
records:
section .text
jmp start
old1 dd 0
old6 dd 0
old13 dd 0
active dw 0
variable dd 12341234h

; name, #UD, SP delta, memory low, AX, CX, flags mask/value, TF, instruction bytes
%macro exercise 10+
    %assign case_count case_count + 1
    %defstr %%case_name %1
    section .rodata
%%name: db %%case_name, ': $'
    section .data
%%record:
    dw %%name
    times 17 dw 0
    dw %2, %3, %4, %5, %6, %7, %8, %9
    section .text
    cli
    mov di,%%record
    mov [active],di
    mov word [di+OP_IP],%%op
    mov word [di+END_IP],%%end
    mov ax,cs
    mov [di+TEST_CS],ax
    mov [di+SP_BEFORE],sp
    mov word [variable],1234h
    mov word [variable+2],1234h
    mov si,di ; A broken LOCK REP MOVS only copies the fixture onto itself.
%ifndef LEGACY
    xor eax,eax
    xor ecx,ecx
%endif
    mov ax,2 | (%9 << 8)
    push ax
    push cs
    mov ax,%%op
    push ax
    mov ax,1
    mov cx,1357h
    iret ; Enable TF exactly at the first prefix, without trapping setup instructions.
%%op:
    db %10
%%end:
    mov di,[active]
    mov [di+SP_AFTER],sp
    mov [di+AX_AFTER],ax
    mov [di+CX_AFTER],cx
    pushf
    pop word [di+FLAGS_AFTER]
    mov bx,sp
    mov ax,[ss:bx]
    mov [di+STACK_TOP],ax
    mov ax,[variable]
    mov [di+MEMORY],ax
    mov ax,[variable+2]
    mov [di+MEMORY_HI],ax
    mov sp,[di+SP_BEFORE]
    sti
%endmacro

start:
    push cs
    pop ds
    mov ax,3506h
    int 21h
    mov [old6],bx
    mov [old6+2],es
    mov ax,3501h
    int 21h
    mov [old1],bx
    mov [old1+2],es
    mov dx,handler6
    mov ax,2506h
    int 21h
    mov ax,350dh
    int 21h
    mov [old13],bx
    mov [old13+2],es
    mov dx,handler13
    mov ax,250dh
    int 21h
    mov dx,handler1
    mov ax,2501h
    int 21h
    push cs
    pop es
%ifndef LEGACY
    push cs
    pop fs
    push cs
    pop gs
%endif

    exercise push_cx,        0,-2,1234h,1,1357h,0,0,0, 51h
    exercise add_mem,        0,0,1235h,1,1357h,08d5h,4,0, 0f0h,01h,06h,MEM
%ifdef LEGACY
%ifdef MODERN
    exercise lock_push_cx,   1,0,1234h,1,1357h,0,0,0, 0f0h,51h
%else
    exercise lock_push_cx,   0,-2,1234h,1,1357h,0,0,0, 0f0h,51h
%endif
%ifdef MODERN
    exercise cmpxchg_mem,    0,0,1234h,1234h,1357h,40h,0,0, 0f0h,0fh,0b1h,0eh,MEM
    exercise xadd_mem,       0,0,1235h,1234h,1357h,08d5h,4,0, 0f0h,0fh,0c1h,06h,MEM
%endif
%else
    exercise ud_control,     1,0,1234h,1,1357h,0,0,0, 0fh,0bh
    exercise lock_push_cx,   1,0,1234h,1,1357h,0,0,0, 0f0h,51h
    exercise add_reg,        1,0,1234h,1,1357h,0,0,0, 0f0h,01h,0c8h
    exercise add_reg_dest,   1,0,1234h,1,1357h,0,0,0, 0f0h,03h,06h,MEM
    exercise add_byte,       0,0,1235h,1,1357h,08d5h,4,0, 0f0h,00h,06h,MEM
    exercise or_mem,         0,0,1235h,1,1357h,0,0,0, 0f0h,09h,06h,MEM
    exercise adc_mem,        0,0,1235h,1,1357h,0,0,0, 0f0h,11h,06h,MEM
    exercise sbb_mem,        0,0,1233h,1,1357h,0,0,0, 0f0h,19h,06h,MEM
    exercise and_mem,        0,0,0000h,1,1357h,0,0,0, 0f0h,21h,06h,MEM
    exercise sub_mem,        0,0,1233h,1,1357h,0,0,0, 0f0h,29h,06h,MEM
    exercise xor_mem,        0,0,1235h,1,1357h,0,0,0, 0f0h,31h,06h,MEM
    exercise cmp_imm,        1,0,1234h,1,1357h,0,0,0, 0f0h,83h,3eh,MEM,1
    exercise add_imm,        0,0,1235h,1,1357h,08d5h,4,0, 0f0h,83h,06h,MEM,1
    exercise add_imm_reg,    1,0,1234h,1,1357h,0,0,0, 0f0h,83h,0c0h,1
    exercise add_imm_byte,   0,0,1235h,1,1357h,0,0,0, 0f0h,80h,06h,MEM,1
    exercise add_imm_word,   0,0,1235h,1,1357h,0,0,0, 0f0h,81h,06h,MEM,1,0
    exercise add_imm_alias,  0,0,1235h,1,1357h,0,0,0, 0f0h,82h,06h,MEM,1
    exercise not_mem,        0,0,0edcbh,1,1357h,0,0,0, 0f0h,0f7h,16h,MEM
    exercise not_reg,        1,0,1234h,1,1357h,0,0,0, 0f0h,0f7h,0d0h
    exercise neg_mem,        0,0,0edcch,1,1357h,0,0,0, 0f0h,0f7h,1eh,MEM
    exercise not_byte,       0,0,12cbh,1,1357h,0,0,0, 0f0h,0f6h,16h,MEM
    exercise test_mem,       1,0,1234h,1,1357h,0,0,0, 0f0h,0f7h,06h,MEM,1,0
    exercise inc_mem,        0,0,1235h,1,1357h,08d5h,4,0, 0f0h,0ffh,06h,MEM
    exercise inc_reg,        1,0,1234h,1,1357h,0,0,0, 0f0h,0ffh,0c0h
    exercise dec_mem,        0,0,1233h,1,1357h,0,0,0, 0f0h,0ffh,0eh,MEM
    exercise inc_byte,       0,0,1235h,1,1357h,0,0,0, 0f0h,0feh,06h,MEM
    exercise push_mem,       1,0,1234h,1,1357h,0,0,0, 0f0h,0ffh,36h,MEM
    exercise xchg_mem,       0,0,0001h,1234h,1357h,0,0,0, 0f0h,87h,06h,MEM
    exercise xchg_byte,      0,0,1201h,34h,1357h,0,0,0, 0f0h,86h,06h,MEM
    exercise xchg_reg,       1,0,1234h,1,1357h,0,0,0, 0f0h,91h
    exercise bts_mem,        0,0,1236h,1,1357h,1,0,0, 0f0h,0fh,0abh,06h,MEM
    exercise btr_mem,        0,0,1234h,1,1357h,1,0,0, 0f0h,0fh,0b3h,06h,MEM
    exercise btc_mem,        0,0,1236h,1,1357h,1,0,0, 0f0h,0fh,0bbh,06h,MEM
    exercise bt_mem,         1,0,1234h,1,1357h,0,0,0, 0f0h,0fh,0a3h,06h,MEM
    exercise bt_imm,         1,0,1234h,1,1357h,0,0,0, 0f0h,0fh,0bah,26h,MEM,2
    exercise bt_imm_reg,     1,0,1234h,1,1357h,0,0,0, 0f0h,0fh,0bah,0e0h,2
    exercise bts_imm,        0,0,1236h,1,1357h,1,0,0, 0f0h,0fh,0bah,2eh,MEM,1
    exercise btr_imm,        0,0,1230h,1,1357h,1,1,0, 0f0h,0fh,0bah,36h,MEM,2
    exercise btc_imm,        0,0,1230h,1,1357h,1,1,0, 0f0h,0fh,0bah,3eh,MEM,2
    exercise bit_bad_group,  1,0,1234h,1,1357h,0,0,0, 0f0h,0fh,0bah,06h,MEM,1
    exercise es_before,      0,0,1235h,1,1357h,0,0,0, 26h,0f0h,01h,06h,MEM
    exercise es_after,       0,0,1235h,1,1357h,0,0,0, 0f0h,26h,01h,06h,MEM
    exercise cs_after,       0,0,1235h,1,1357h,0,0,0, 0f0h,2eh,01h,06h,MEM
    exercise ss_after,       0,0,1235h,1,1357h,0,0,0, 0f0h,36h,01h,06h,MEM
    exercise ds_after,       0,0,1235h,1,1357h,0,0,0, 0f0h,3eh,01h,06h,MEM
    exercise fs_after,       0,0,1235h,1,1357h,0,0,0, 0f0h,64h,01h,06h,MEM
    exercise gs_after,       0,0,1235h,1,1357h,0,0,0, 0f0h,65h,01h,06h,MEM
    exercise addr32,         0,0,1235h,1,1357h,0,0,0, 0f0h,67h,01h,05h,MEM,0,0
    exercise operand32,      0,0,1235h,1,1357h,0,0,0, 0f0h,66h,01h,06h,MEM
    exercise duplicate_lock, 0,0,1235h,1,1357h,0,0,0, 0f0h,0f0h,01h,06h,MEM
    exercise repne_add,      0,0,1235h,1,1357h,0,0,0, 0f0h,0f2h,01h,06h,MEM
    exercise rep_add,        0,0,1235h,1,1357h,0,0,0, 0f0h,0f3h,01h,06h,MEM
    exercise lock_push32,    1,0,1234h,1,1357h,0,0,0, 0f0h,66h,51h
    exercise size_before,    1,0,1234h,1,1357h,0,0,0, 66h,0f0h,51h
    exercise rep_movs,       1,0,1234h,1,1357h,0,0,0, 0f0h,0f3h,0a5h
    ; 15 bytes is the 386 limit; one more byte raises #GP (reported as 100h).
    exercise length_15,      0,0,1235h,1,1357h,0,0,0, 26h,26h,26h,26h,26h,26h,26h,26h,26h,26h,0f0h,01h,06h,MEM
    exercise length_16,      100h,0,1234h,1,1357h,0,0,0, 26h,26h,26h,26h,26h,26h,26h,26h,26h,26h,26h,0f0h,01h,06h,MEM
    exercise length_16_imm,  100h,0,1234h,1,1357h,0,0,0, 26h,26h,26h,26h,26h,26h,0f0h,66h,81h,06h,MEM,1,0,0,0
    exercise ud_tf,          1,0,1234h,1,1357h,0,0,1, 0fh,0bh
    exercise push_tf,        0,-2,1234h,1,1357h,0,0,1, 51h
    exercise add_tf,         0,0,1235h,1,1357h,08d5h,4,1, 0f0h,01h,06h,MEM
    exercise lock_push_tf,   1,0,1234h,1,1357h,0,0,1, 0f0h,51h
%endif

    mov dx,[old1]
    push ds
    mov ax,[old1+2]
    mov ds,ax
    mov ax,2501h
    int 21h
    pop ds
    mov dx,[old6]
    push ds
    mov ax,[old6+2]
    mov ds,ax
    mov ax,2506h
    int 21h
    pop ds
    mov dx,[old13]
    push ds
    mov ax,[old13+2]
    mov ds,ax
    mov ax,250dh
    int 21h
    pop ds

    mov di,records
    mov bp,case_count
.record:
    mov dx,[di]
    mov ah,9
    int 21h
    lea si,[di+2]
    mov cx,(REC_SIZE-2)/2
.word:
    lodsw
    call hexword
    mov dl,' '
    mov ah,2
    int 21h
    loop .word
    mov dx,newline
    mov ah,9
    int 21h
    add di,REC_SIZE
    dec bp
    jnz .record
    mov dx,done
    mov ah,9
    int 21h
    mov ax,4c00h
    int 21h

handler13:
    push bp
    mov bp,sp
    push ax
    push di
    mov di,[cs:active]
    add word [cs:di+UD],100h
    jmp fault_common

handler6:
    push bp
    mov bp,sp
    push ax
    push di
    mov di,[cs:active]
    inc word [cs:di+UD]
fault_common:
    mov ax,[ss:bp+2]
    mov [cs:di+UD_IP],ax
    mov ax,[ss:bp+4]
    mov [cs:di+UD_CS],ax
    mov ax,[cs:di+END_IP]
    mov [ss:bp+2],ax
    and word [ss:bp+6],0feffh
    pop di
    pop ax
    pop bp
    iret

handler1:
    push bp
    mov bp,sp
    push ax
    push di
    mov di,[cs:active]
    mov ax,[ss:bp+2]
    cmp word [cs:di+DB],0
    jne .have_first
    mov [cs:di+DB_FIRST],ax
.have_first:
    inc word [cs:di+DB]
    mov [cs:di+DB_LAST],ax
    cmp ax,[cs:di+END_IP]
    je .stop_tf
    cmp ax,handler6 ; Preserve the emulator's existing post-fault trap behavior.
    jne .continue_tf
.stop_tf:
    and word [ss:bp+6],0feffh
.continue_tf:
    pop di
    pop ax
    pop bp
    iret

hexword:
    push ax
    push bx
    push cx
    push dx
    mov bx,ax
    mov cx,4
.digit:
    rol bx,1
    rol bx,1
    rol bx,1
    rol bx,1
    mov dl,bl
    and dl,0fh
    add dl,'0'
    cmp dl,'9'
    jbe .emit
    add dl,7
.emit:
    mov ah,2
    int 21h
    loop .digit
    pop dx
    pop cx
    pop bx
    pop ax
    ret

newline db 13,10,'$'
done db 'DONE',13,10,'$'
