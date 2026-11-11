; ACME: -DC16=0 or -DC16=1. BOOT=1 selects the tape-bootstrap handoff.
; Descriptor: destination, length, execution address (three little-endian words).
; Owns $f7..$ff and stack. Payload must not overlap receiver/tables.
!cpu 6502
!ifndef BLOCK_SIZE { BLOCK_SIZE = 256 }
!ifndef BOOT { BOOT = 0 }
!ifndef ORIGIN { ORIGIN = $0800 }
* = ORIGIN
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
    ; The final READ poll also sampled physical WRITE at P6 into V.
    lda $fd10
    and #$04                    ; SENSE remains bit 2
    bvc +
    ora #$02
+
    lsr
    sta result                  ; upper nibble bits 0/1

    lda #$10
-   bit $01
    beq -                       ; second READ high
    ; The final READ poll also sampled physical WRITE at P6 into V.
    lda $fd10
    and #$04                    ; SENSE remains bit 2
    bvc +
    ora #$02
+
    asl
    ora result
    tax                         ; complete upper nibble, 0..15

    lda #$10
-   bit $01
    bne -                       ; third READ low
    ; The final READ poll also sampled physical WRITE at P6 into V.
    lda $fd10
    and #$04                    ; SENSE remains bit 2
    bvc +
    ora #$02
+
    lsr
    sta result                  ; lower nibble bits 0/1

    lda #$10
-   bit $01
    beq -                       ; fourth READ high
    ; The final READ poll also sampled physical WRITE at P6 into V.
    lda $fd10
    and #$04                    ; SENSE remains bit 2
    bvc +
    ora #$02
+
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
!if BOOT {
    !if C16 { lda #$0b } else { lda #$27 }
    sta $00                     ; bootstrap already released WRITE
}
    ldx #$ff
    txs
!if C16 {
    lda $ff06
    and #$ef
    sta $ff06
    lda #$ff
    sta $fd10
    lda #$0b                    ; P1 output low releases the 7406 WRITE sink
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
!if BOOT = 0 {
    lda #$2f
    sta $00
    lda #$0f
    sta $01                     ; cold monitor test primes input pull-up
}
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
!if BOOT {
    lda dest
    sta saved_start
    lda dest+1
    sta saved_start+1
}
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
!if BOOT {
    ; Relocate the final handoff into the now-unused tape buffer. Screen
    ; initialization can then clear screen/color RAM containing this code.
    lda entry
    sta finish_source+(launch_entry-finish)
    lda entry+1
    sta finish_source+(launch_entry-finish)+1
    lda saved_start
    sta finish_source+(program_start-finish)
    lda saved_start+1
    sta finish_source+(program_start-finish)+1
    lda dest
    sta finish_source+(program_end-finish)
    lda dest+1
    sta finish_source+(program_end-finish)+1
    ldx #finish_end-finish-1
copy_finish:
    lda finish_source,x
    sta finish,x
    dex
    bpl copy_finish
    jmp finish
} else {
    jmp (entry)                 ; test stops before executing the payload
}

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
!if BOOT {
saved_start: !word 0
finish_source:
!if C16 { FINISH_ADDR = $0333 } else { FINISH_ADDR = $033c }
!pseudopc FINISH_ADDR {
finish:
    sei
    jsr $ff84                   ; IOINIT: ports/timers back to KERNAL state
    jsr $ff8a                   ; restore KERNAL vectors
!if C16 { jsr $8117 } else { jsr $e453 } ; restore BASIC vectors, including $0302
    jsr $ff81                   ; screen/color init can overwrite receiver
    lda program_start
    sta $2b
    lda program_start+1
    sta $2c
    lda program_end
    sta $2d
    lda program_end+1
    sta $2e
    lda #0
    sta $90                     ; clear cassette status
!if C16 { sta $ef } else { sta $c6 }
    lda launch_entry
    ora launch_entry+1
    beq run_basic
    cli
    jmp (launch_entry)
run_basic:
!if C16 {
    jsr $8bbe                   ; CLR / reset BASIC execution pointers
    cli
    jmp $8bea
} else {
    jsr $a659                   ; CLR / reset BASIC execution pointers
    cli
    jmp $a7ae
}
launch_entry: !word 0            ; zero selects BASIC RUN
program_start: !word 0
program_end: !word 0
finish_end:
}
!if finish_end-finish > 128 { !error "finish trampoline exceeds copy loop" }
}
receiver_end:
