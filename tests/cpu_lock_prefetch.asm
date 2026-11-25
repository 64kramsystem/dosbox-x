; SPDX-License-Identifier: GPL-2.0-or-later
; Self-modifying LOCK test: a prefetching 386 executes the queued (valid) bytes,
; other cores the rewritten (invalid) ones. Prints O for the queued bytes, U for #UD.
bits 16
org 100h
    mov dx,ud
    mov ax,2506h
    int 21h
    mov ax,1
    jmp short go        ; flush the queue
go: mov byte [cs:target+1],03h   ; rewrite 01 (ADD m,r: lockable) to 03 (ADD r,m: not lockable)
target:
    lock add [var],ax
    mov dl,'O'
    cmp word [var],1
    je out
    mov dl,'?'
out:mov ah,2
    int 21h
    mov ax,4c00h
    int 21h
ud: mov dl,'U'
    mov ah,2
    int 21h
    mov ax,4c00h
    int 21h
var dw 0
