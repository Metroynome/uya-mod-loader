/* Wrap the sync function at its entry, leaving Horizon's return hook alone.
 * Run the original body first so module drawing keeps the old return timing.
 * Each trampoline replays the two entry instructions replaced by J / NOP.
 */
.set noreorder
.section .entry,"ax",@progbits
.globl campaignEntry
campaignEntry:
	addiu $sp, $sp, -64
	sq $ra, 0($sp)
	sq $gp, 48($sp)
	jal campaignOriginal
	nop
	sq $v0, 16($sp)
	sq $v1, 32($sp)
	jal loaderTick
	li $a0, 1
	b restore
	nop
.globl multiplayerEntry
multiplayerEntry:
	addiu $sp, $sp, -64
	sq $ra, 0($sp)
	sq $gp, 48($sp)
	jal multiplayerOriginal
	nop
	sq $v0, 16($sp)
	sq $v1, 32($sp)
	jal loaderTick
	li $a0, 2
restore:
	lq $gp, 48($sp)
	lq $v1, 32($sp)
	lq $v0, 16($sp)
	lq $ra, 0($sp)
	jr $ra
	addiu $sp, $sp, 64

campaignOriginal:
	addiu $sp, $sp, -32
	sd $s0, 0($sp)
	j 0x0012c910
	nop
multiplayerOriginal:
	addiu $sp, $sp, -32
	sd $s0, 0($sp)
	j 0x00139e10
	nop

/* BIOS syscall 0x7f; PCSX2 returns its exposed EE RAM size here. */
.globl platformMemorySize
platformMemorySize:
	addiu $v1, $zero, 0x7f
	syscall
	jr $ra
	nop
