.global _main

// SAlloc 4
// SAlloc 4
// SAlloc 4
printch:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	str w0, [sp, #8]
	str w1, [sp, #4]
	ldr w8, [sp, #8]
	ldr w9, [sp, #4]
	add w8, w8, w9
	str w8, [sp, #4]
	mov w0, w8
	bl _putchar
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
// SAlloc 4
_main:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	mov w8, #2
	str w8, [sp, #12]
	mov w0, #65
	ldr w1, [sp, #12]
	bl printch
	mov w0, #66
	ldr w1, [sp, #12]
	bl printch
	mov w0, #0
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
