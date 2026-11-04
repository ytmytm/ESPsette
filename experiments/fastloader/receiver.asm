; ACME: -DC16=0 or -DC16=1. Experimental receiver, entered by VICE monitor.
; Not yet a KERNAL tape bootstrap. Receives a six-byte descriptor:
; destination, length, execution address (all little endian), then payload.
; The destination must not overlap this code, zero page $f7-$ff or stack.
!cpu 6502
!ifndef BLOCK_SIZE { BLOCK_SIZE = 256 }
* = $0800
dest = $f7
remaining = $f9
entry = $fb
result = $fd
block_remaining = $fe
!if C16 {
    expected_read = $ff
    motor_mask = $08
} else {
    motor_mask = $20
}
start:
    sei
    ldx #$ff
    txs
!if C16 {
    lda $ff06
    and #$ef
    sta $ff06                  ; blank TED; full speed outside refresh
    lda #$ff
    sta $fd10                  ; release SENSE on Plus/4 6529
    lda #$09                   ; MOTOR + IEC DATA out; WRITE input
    sta $00
    lda #$00                   ; MOTOR on, known initial handshake state
    sta expected_read
    sta $01
} else {
    lda $d011
    and #$ef
    sta $d011
    lda #$00
    sta $d015                  ; disable sprite DMA as well as badlines
    lda #$7f
    sta $dc0d                  ; mask CIA interrupts; FLAG latch still works
    sta $dd0d                  ; mask CIA2 NMI sources
    lda $dc0d                  ; clear pending FLAG before first request
    lda $dd0d
    lda #$2f
    sta $00
    lda #$0f                   ; release WRITE high before changing direction
    sta $01
    lda #$27                   ; WRITE and SENSE inputs, MOTOR output
    sta $00
    lda #$07                   ; keep ROM and I/O, MOTOR on
    sta $01
}
    sta port_value
    ; Allow display disable to take effect (including the current badline).
    ldx #$00
settle:
    dex
    bne settle
    ldy #$00
    jsr request_block
    lda #6
    sta block_remaining         ; descriptor is its own six-byte block
header:
    jsr getbyte
    sta dest,y
    iny
    cpy #6
    bne header
    ldy #0
load:
    jsr getbyte
    sta (dest),y
    inc dest
    bne no_carry
    inc dest+1
no_carry:
    lda remaining
    bne no_borrow
    dec remaining+1
no_borrow:
    dec remaining
    lda remaining
    ora remaining+1
    bne load
done:
    jmp (entry)                ; test payload loops at its execution address

request_block:
    lda port_value             ; never RMW $01: input bits contain live data!
    eor #motor_mask            ; one MOTOR transition requests one block
    sta port_value
    sta $01
    lda #BLOCK_SIZE & $ff       ; zero means 256 bytes until DEC wraps
    sta block_remaining
    rts

getbyte:
    lda block_remaining
    bne have_block
    jsr request_block
have_block:
    lda #0
    sta result
    ldx #4
pair:
!if C16 {
wait_read:
    lda $01
    eor expected_read
    and #$10
    bne wait_read
    ; WRITE is CPU port bit 1, SENSE is $fd10 bit 2. Separate reads
    ; require data to stay stable until both samples have completed.
    lda $01
    lsr
    lsr
    rol result
    lda $fd10
    lsr
    lsr
    lsr
    rol result
    lda expected_read
    eor #$10
    sta expected_read
} else {
wait_flag:
    lda $dc0d                  ; read acknowledges ALL pending CIA1 sources
    and #$10                   ; bit 4: FLAG, not IRQ summary bit 7
    beq wait_flag
    lda $01                    ; physical WRITE bit 3, SENSE bit 4
    pha
    lsr
    lsr
    lsr
    lsr
    rol result
    pla
    lsr
    lsr
    lsr
    lsr
    lsr
    rol result
}
    dex
    bne pair
    dec block_remaining
    lda result
    rts
port_value: !byte 0
