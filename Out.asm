.global _main

// SAlloc 4
// SAlloc 4
// SAlloc 4
// SAlloc 4
TestFn:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	str w0, [sp, #8]
	str w1, [sp, #4]
	str w2, [sp, #0]
	ldr w8, [sp, #8]
	ldr w9, [sp, #4]
	add w8, w8, w9
	ldr w10, [sp, #0]
	add w8, w8, w10
	str w8, [sp, #0]
	mov w0, #65
	bl _putchar
	ldr w0, [sp, #0]
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
// SAlloc 4
_main:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	mov w0, #3
	mov w1, #2
	mov w2, #1
	bl TestFn
	str w0, [sp, #12]
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
