.global _main

_R_0:
                         	// Offset: 0
mov w0, #5
ret                      	// Offset: 2
// End of frame          	// Offset: 8
// salloc 4              	// Offset: 10
// salloc 4              	// Offset: 14
// Entry point           	// Offset: 18
// Branches              	// Offset: 20
_main:
sub sp, sp, #32
stp x29, x30, [sp, #16]
add x29, sp, #16         	// Offset: 22
// params begin          	// Offset: 24
bl _R_0                  	// Offset: 26
str w0, [sp, #12]        	// Offset: 32
ldr w8, [sp, #12]        	// Offset: 38
mov w9, #2               	// Offset: 44
add w8, w8, w9           	// Offset: 50
str w8, [sp, #8]         	// Offset: 54
ldr w0, [sp, #8]         	// Offset: 60
ldp x29, x30, [sp, #16]
add sp, sp, #32
ret                      	// Offset: 66
// End of frame          	// Offset: 70
