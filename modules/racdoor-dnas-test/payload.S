.section .text
.balign 16
.globl dnasPayloadStart
.type dnasPayloadStart, @function
dnasPayloadStart:
.incbin "../racdoor/samples/uya-dnas-pal/uya-dnas-pal.bin"
.size dnasPayloadStart, .-dnasPayloadStart
