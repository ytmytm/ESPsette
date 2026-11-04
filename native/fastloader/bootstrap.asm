; Autostart code carried in the standard tape HEADER; data body is only the
; two-byte $0302 vector. Downloads padded receiver pages to $0400 (C64) or
; $0800 (TED), then jumps. WRITE is input after the first seven CPU cycles.
!cpu 6502
!ifndef STAGE_PAGES { STAGE_PAGES = 3 }
!ifndef STAGE_ADDR {
    !if C16 { STAGE_ADDR = $0800 } else { STAGE_ADDR = $0400 }
}
ptr = $f7
result = $fd
expected = $ff
!if C16 {
    * = $0347                   ; header type is stored separately at $f8
    motor_mask = $08
    write_mask = $02
    port_off = $0a              ; physical WRITE low (7501 output inverts)
} else {
    * = $0351
    motor_mask = $20
    write_mask = $08
    port_off = $27
}
start:
    sei
!if C16 { lda #$09 } else { lda #$27 }
    sta $00                     ; release WRITE immediately on autostart
    ldx #$ff
    txs
!if C16 {
    lda $ff06
    and #$ef
    sta $ff06
    lda #$0b
} else {
    lda $d011
    and #$ef
    sta $d011
    lda #0
    sta $d015
    lda #$7f
    sta $dc0d
    sta $dd0d
    lda #$2f
}
    sta $00                     ; WRITE output during CA65 mode selection
    ldy #16
signature_loop:
    lda #port_off
    asl signature
    rol signature+1
    bcc zero_bit
    eor #write_mask
zero_bit:
    sta $01                     ; MOTOR off, WRITE already stable
    ldx #0
off_delay:
    dex
    nop
    nop
    bne off_delay               ; >1 ms even at TED fast NTSC clock
    and #$ff-motor_mask
    sta $01                     ; rising MOTOR samples the signature bit
    dey
    beq signature_done          ; release WRITE immediately after bit 16
    ldx #64
on_delay:
    dex
    bne on_delay
    jmp signature_loop
signature_done:
!if C16 {
    lda #$09
    sta $00                     ; release WRITE
    lda #0
    sta expected
} else {
    lda #$27
    sta $00
    lda $dc0d                   ; clear standard-tape FLAG residue
    lda #$07
}
    sta port_value
    sta $01                     ; known initial MOTOR on
    lda #0
    sta ptr
    lda #>STAGE_ADDR
    sta ptr+1
    ldy #0
page:
    lda port_value
    eor #motor_mask
    sta port_value
    sta $01
byte:
    jsr getbyte
    sta (ptr),y
    iny
    bne byte
    inc ptr+1
    lda ptr+1
    cmp #(>STAGE_ADDR)+STAGE_PAGES
    bne page
    jmp STAGE_ADDR
; Compact header receiver: pairs b7:b6, b5:b4, b3:b2, b1:b0.
; Serial shifts and a shared loop save space within the 171-byte header tail.
; Only receiver pages use this format; the full receiver uses optimized pairs.
getbyte:
    lda #0
    sta result
    ldx #4
pair:
!if C16 {
wait:
    lda $01
    eor expected
    and #$10
    bne wait
    lda $01
    lsr
    lsr
    rol result
    lda $fd10
    lsr
    lsr
    lsr
    rol result
    lda expected
    eor #$10
    sta expected
} else {
wait:
    lda $dc0d
    and #$10
    beq wait
    lda $01
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
    lda result
    rts
port_value: !byte 0
signature: !word $ca65
end:
!if C16 {
    !if end > $03f2 { !error "C16 bootstrap does not fit tape header" }
} else {
    !if end > $03fc { !error "C64 bootstrap does not fit tape header" }
}
