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

_R_0:
sub sp, sp, #16
stp x29, x30, [sp, #16]
add x29, sp, #16         	// Offset: 0
mov w0, #5
add sp, sp, #16
ret                      	// Offset: 2
// End of frame          	// Offset: 8
// salloc 4              	// Offset: 10
// salloc 4              	// Offset: 14
// Entry point           	// Offset: 18
_main:
sub sp, sp, #32
stp x29, x30, [sp, #16]
add x29, sp, #16         	// Offset: 20
// params begin          	// Offset: 22
bl _R_0                  	// Offset: 24
str w0, [sp, #12]        	// Offset: 30
ldr w8, [sp, #12]        	// Offset: 36
mov w9, #2               	// Offset: 42
add w8, w8, w9           	// Offset: 48
str w8, [sp, #8]         	// Offset: 52
add sp, sp, #32
// End of frame          	// Offset: 58
.Lexit:
    mov w0, w8
    b _exit
