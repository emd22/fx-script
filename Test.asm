# .global _main
# _main:
#     // salloc 4              	// Offset: 0
#     // salloc 4              	// Offset: 4
#     sub sp, sp, #32          	// Offset: 8

#     stp x30, x29, [sp, #16]
#     add sp, sp, #16

#     mov w8, #5
#     str w8, [sp, #12]        	// Offset: 10
#     ldr w8, [sp, #12]        	// Offset: 18
#     mov w9, #2               	// Offset: 24
#     add w8, w8, w9           	// Offset: 30
#     str w8, [sp, #8]         	// Offset: 34
#     add sp, sp, #16          	// Offset: 40

# .Lexit:         // Bootstrap exit
#     mov w0, w8
#     b _exit

.global _main

take_five:
        sub     sp, sp, #16
        str     w0, [sp, #12]
        ldr     w8, [sp, #12]
        add     w0, w8, #5
        add     sp, sp, #16
        ret

_main:
        sub     sp, sp, #32
        stp     x29, x30, [sp, #16]
        add     x29, sp, #16
        mov     w8, wzr
        str     w8, [sp]
        stur    wzr, [x29, #-4]
        mov     w0, #10
        bl      take_five
        mov     w8, w0
        ldr     w0, [sp]
        str     w8, [sp, #8]
        ldr     w8, [sp, #8]
        add     w8, w8, #2
        str     w8, [sp, #4]
        ldp     x29, x30, [sp, #16]
        add     sp, sp, #32
        # ret

.Lexit:         // Bootstrap exit
    mov w0, w8
    b _exit
