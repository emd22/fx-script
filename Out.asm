.global _main

// SAlloc 4
TestFn:
	sub sp, sp, #16
	add w8, w8, w9
	add w8, w8, w10
	str w8, [sp, #12]
	mov w0, w8
	add sp, sp, #16
	ret
// SAlloc 4
_main:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	mov w8, #3
	mov w9, #2
	mov w10, #1
	bl TestFn
	str w0, [sp, #12]
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
