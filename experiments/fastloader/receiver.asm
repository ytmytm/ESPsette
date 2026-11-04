; ACME: -DC16=0 or -DC16=1. Experimental receiver entered by VICE monitor.
; Descriptor: destination, length, execution address (three little-endian words).
; Owns $f7..$ff and stack. Payload must not overlap receiver/tables.
!cpu 6502
!ifndef BLOCK_SIZE { BLOCK_SIZE = 256 }
* = $0800
dest = $f7
remaining = $f9
entry = $fb
result = $fd
block_remaining = $fe
write_bit = $ff
!if C16 { motor_mask = $08 } else { motor_mask = $20 }

; On both machines WRITE/SENSE carry b4/b5, b6/b7, b0/b1, b2/b3.
; A ready BYTE is returned in A; X is scratch, Y remains the destination index.
!macro receive_byte {
!if C16 {
    lda #$10
-   bit $01
    bne -                       ; first READ low
    lda $01
    and #$02
    sta write_bit
    lda $fd10
    and #$04
    ora write_bit
    lsr
    sta result                  ; upper nibble bits 0/1

    lda #$10
-   bit $01
    beq -                       ; second READ high
    lda $01
    and #$02
    sta write_bit
    lda $fd10
    and #$04
    ora write_bit
    asl
    ora result
    tax                         ; complete upper nibble, 0..15

    lda #$10
-   bit $01
    bne -                       ; third READ low
    lda $01
    and #$02
    sta write_bit
    lda $fd10
    and #$04
    ora write_bit
    lsr
    sta result                  ; lower nibble bits 0/1

    lda #$10
-   bit $01
    beq -                       ; fourth READ high
    lda $01
    and #$02
    sta write_bit
    lda $fd10
    and #$04
    ora write_bit
    asl
    ora result
    ora upper_table,x
} else {
    ; Poll the actual FLAG bit (4), not the delayed IRQ summary (7).
    ; No stack save is needed: both bits are assembled together per sample.
    lda #$10
-   bit $dc0d
    beq -
    lda $01
    and #$18
    lsr
    lsr
    sta result
    lda #$10
-   bit $dc0d
    beq -
    lda $01
    and #$18
    ora result
    lsr
    tax                         ; complete upper nibble, 0..15
    lda #$10
-   bit $dc0d
    beq -
    lda $01
    and #$18
    lsr
    lsr
    sta result
    lda #$10
-   bit $dc0d
    beq -
    lda $01
    and #$18
    ora result
    lsr
    ora upper_table,x
}
}

start:
    sei
    ldx #$ff
    txs
!if C16 {
    lda $ff06
    and #$ef
    sta $ff06
    lda #$ff
    sta $fd10
    lda #$09                    ; MOTOR output, WRITE input
    sta $00
    lda #$00
    sta $01
} else {
    lda $d011
    and #$ef
    sta $d011
    lda #0
    sta $d015
    lda #$7f
    sta $dc0d                   ; disable all CIA1 IRQ sources
    sta $dd0d
    lda $dc0d
    lda $dd0d
    lda #$2f
    sta $00
    lda #$0f
    sta $01                     ; WRITE high before switching to input
    lda #$27
    sta $00
    lda #$07                    ; keep ROM/I/O visible, MOTOR on
    sta $01
}
    sta port_value
    ldx #0
settle:
    dex
    bne settle
    jsr request_block
    ldy #0
    jmp header

; Each unrolled receive body and its loop fit within a 128-byte window:
; neither polling nor the outer loop gains a branch page-crossing cycle.
!align 127, 0
header:
    +receive_byte
header_store:
    sta dest,y
    iny
    cpy #6
    bne header
header_end:
    ldy #0
next_block:
!if BLOCK_SIZE = 256 {
    lda remaining+1
    bne full_block
    lda remaining
    bne partial_block
    jmp done
partial_block:
} else {
    lda remaining
    ora remaining+1
    bne single_byte_block
    jmp done
single_byte_block:
    lda remaining
    bne no_borrow
    dec remaining+1
no_borrow:
    dec remaining
    lda #1
}
    ; A is the final block length n (or 1 in the comparison mode).
    ; Use Y = 256-n and base = destination+n-256. Every effective address
    ; remains destination+offset, while Y wrapping ends exactly n writes.
    sta block_remaining         ; temporary n, never decremented per byte
    clc
    adc dest
    sta dest
    bcs no_base_borrow
    dec dest+1
no_base_borrow:
    lda #0
    sec
    sbc block_remaining
    tay
!if BLOCK_SIZE = 256 {
    lda #0
    sta remaining
}
    jmp block_ready
!if BLOCK_SIZE = 256 {
full_block:
    dec remaining+1             ; subtract 256 once; Y is already zero
}
block_ready:
    jsr request_block
    jmp load

!align 127, 0
load:
    +receive_byte               ; inline: no per-byte JSR/RTS
payload_store:
    sta (dest),y
    iny
    bne load                    ; Y is also the block counter
    inc dest+1                  ; wrapping advances the base by 256
    jmp next_block
load_end:
done:
    jmp (entry)                 ; test stops before executing the payload

request_block:
    lda port_value              ; never derive output latch from live $01
    eor #motor_mask
    sta port_value
    sta $01
    rts
port_value: !byte 0

!align 15, 0                    ; no indexed-read page crossing, X is 0..15
upper_table:
!for i, 0, 15 {
    !byte i << 4
}
receiver_end:
