	.section	__TEXT,__text,regular,pure_instructions
	.build_version macos, 15, 0	sdk_version 15, 5
	.globl	_some_function                  ; -- Begin function some_function
	.p2align	2
_some_function:                         ; @some_function
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #16
	.cfi_def_cfa_offset 16
	str	w0, [sp, #12]
	ldr	w8, [sp, #12]
	add	w0, w8, #5
	add	sp, sp, #16
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_main                           ; -- Begin function main
	.p2align	2
_main:                                  ; @main
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #32
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	add	x29, sp, #16
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	mov	w8, #0                          ; =0x0
	str	w8, [sp]                        ; 4-byte Folded Spill
	stur	wzr, [x29, #-4]
	mov	w0, #10                         ; =0xa
	bl	_some_function
	mov	x8, x0
	ldr	w0, [sp]                        ; 4-byte Folded Reload
	str	w8, [sp, #8]
	ldr	w8, [sp, #8]
	add	w8, w8, #2
	str	w8, [sp, #4]
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	add	sp, sp, #32
	ret
	.cfi_endproc
                                        ; -- End function
.subsections_via_symbols
