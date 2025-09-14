.global _main

_R_0:
sub sp, sp, #16
stp x29, x30, [sp, #0]
add x29, sp, #16         	// Offset: 0
mov w0, #5
ldp x29, x30, [sp, #0]
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
ldr w0, [sp, #8]         	// Offset: 58
ldp x29, x30, [sp, #16]
add sp, sp, #32
ret                      	// Offset: 64
// End of frame          	// Offset: 68
