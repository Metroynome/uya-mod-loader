/* The patched jr ra keeps its original stack-restoring delay slot.
 * The argument identifies the SDK layout; C checks the current level ID.
 * Save full EE return registers and gp before calling C/module code.
 */
.set noreorder
.section .entry,"ax",@progbits
.globl campaignEntry
campaignEntry:
    addiu $sp, $sp, -64
    sq $ra, 0($sp)
    sq $v0, 16($sp)
    sq $v1, 32($sp)
    sq $gp, 48($sp)
    jal loaderTick
    li $a0, 1
    b restore
    nop
.globl multiplayerEntry
multiplayerEntry:
    addiu $sp, $sp, -64
    sq $ra, 0($sp)
    sq $v0, 16($sp)
    sq $v1, 32($sp)
    sq $gp, 48($sp)
    jal loaderTick
    li $a0, 2
restore:
    lq $gp, 48($sp)
    lq $v1, 32($sp)
    lq $v0, 16($sp)
    lq $ra, 0($sp)
    jr $ra
    addiu $sp, $sp, 64
