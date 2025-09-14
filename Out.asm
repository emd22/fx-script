.global _main

_R_0:
	mov w0, #2
	ret
// SAlloc 4
_R_16:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	bl _R_0
	push [r32] RETVAL
	pop [i32] GW0
	mov w9, #3
	add w8, w8, w9
	str w8, [sp, #12]
	ldr w0, [sp, #12]
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
// SAlloc 4
// SAlloc 4
_main:
	sub sp, sp, #32
	stp x29, x30, [sp, #16]
	add x29, sp, #16
	bl _R_10
	str w0, [sp, #12]
	ldr w8, [sp, #12]
	mov w9, #2
	add w8, w8, w9
	str w8, [sp, #8]
	ldr w0, [sp, #8]
	ldp x29, x30, [sp, #16]
	add sp, sp, #32
	ret
