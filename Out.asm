.global _main

// SAlloc 4
C_putchar:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	str w0, [sp, #12]
	ldr w0, [sp, #12]
	bl _putchar
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
_main:
	sub sp, sp, #16
	stp x29, x30, [sp, #0]
	add x29, sp, #16
	mov w0, #65
	bl C_putchar
	mov w0, #66
	bl C_putchar
	mov w0, #0
	ldp x29, x30, [sp, #0]
	add sp, sp, #16
	ret
