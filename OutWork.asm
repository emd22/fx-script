.global _main

printc:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	str w0, [sp, #8]
	ldr w8, [sp, #8]
	str w8, [sp, #8]
	mov w0, w8
	bl _putchar
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
_main:
	sub sp, sp, #16
	stp x29, x30, [sp, #0]
	add x29, sp, #16
	mov w0, #65
	bl printc
	mov w0, #0
	ldp x29, x30, [sp, #0]
	add sp, sp, #16
	ret
