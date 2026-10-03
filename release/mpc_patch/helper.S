@ Replaces the 16-byte helper at 0x2494954 (MPC 3.9.1.2; its only caller is 0x24fe2dc): jump to the name matcher (names.S).
        .arm
        .text
        .globl _start
_start:
        ldr   r2, [r0]
        ldr   ip, [pc, #0]
        add   pc, pc, ip
        .word 0x6872980 - 0x2494964
