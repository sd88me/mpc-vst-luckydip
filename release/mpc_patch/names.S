@ Drum-layout name matcher for MPC 3.9.1.2. Linked at 0x6872980: zero padding after the R+X LOAD segment's file data
@ (ends 0x687288c), inside its last mapped page (ends 0x6873000), after colours.S. Nothing references it.
@ In: r0 = juce::String* (the plugin's effGetEffectName), lr = return address into the caller (0x24fe2e0).
@ Out: r0 = 1 when the name is in the table below, else the original check's result (String == "DrumSynth:Multi").
@ Add a plugin: append one .asciz line to the table (the page tail has ~1.4 KB free). The table ends with an empty string.
        .arm
        .text
        .globl _start
        .equ  BASE, 0x6872980
_start:
        adr   r1, names
entry:  ldr   r2, [r0]              @ the String's UTF-8 text (restart for each table entry)
cmpl:   ldrb  r3, [r2], #1
        ldrb  ip, [r1], #1
        cmp   r3, ip
        bne   skip
        cmp   r3, #0
        bne   cmpl
        mov   r0, #1                @ listed: drum layout
        bx    lr
skip:   cmp   ip, #0                @ the table entry ended first: r1 is already at the next entry
        beq   more
sk2:    ldrb  ip, [r1], #1          @ else run to the end of this entry
        cmp   ip, #0
        bne   sk2
more:   ldrb  ip, [r1]
        cmp   ip, #0                @ an empty string ends the table
        bne   entry
        ldr   r1, L1                @ not listed: the original check as a tail call (r0 still the String*, lr intact)
A1:     add   r1, pc, r1            @ r1 = 0x4ab2760 "DrumSynth:Multi"
        ldr   ip, L2
A2:     add   pc, pc, ip            @ -> 0x9289ec  bool juce::String::operator==(const char*)
L1:     .word 0x4ab2760 - BASE - (A1 - _start) - 8
L2:     .word 0x9289ec  - BASE - (A2 - _start) - 8
names:  .asciz "Machinemodule"
        .asciz "Lucky Dip"
        .byte 0
