.syntax         unified
.cpu            arm7tdmi
 .section        .text._fini,"ax",%progbits
_fini:
  bx lr
 .global _fini
 @ vim: ft=armv4 et sta sw=4 sts=8
