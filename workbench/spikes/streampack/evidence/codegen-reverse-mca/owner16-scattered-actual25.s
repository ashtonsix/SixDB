.text
    ldr	q2, [x21, w22, uxtw] // linked 0x00018e14
    sub	w6, w19, w6 // linked 0x00018e18
    ushll	v3.8h, v2.8b, #0x0 // linked 0x00018e1c
    ushll2	v4.8h, v2.16b, #0x0 // linked 0x00018e20
    ext	v3.16b, v0.16b, v3.16b, #0xe // linked 0x00018e24
    ext	v4.16b, v0.16b, v4.16b, #0xe // linked 0x00018e28
    uaddw	v3.8h, v3.8h, v2.8b // linked 0x00018e2c
    uaddw2	v2.8h, v4.8h, v2.16b // linked 0x00018e30
    ext	v4.16b, v0.16b, v3.16b, #0xc // linked 0x00018e34
    add	v3.8h, v4.8h, v3.8h // linked 0x00018e38
    ext	v4.16b, v0.16b, v2.16b, #0xc // linked 0x00018e3c
    add	v2.8h, v4.8h, v2.8h // linked 0x00018e40
    ext	v4.16b, v0.16b, v3.16b, #0x8 // linked 0x00018e44
    add	v3.8h, v4.8h, v3.8h // linked 0x00018e48
    ext	v4.16b, v0.16b, v2.16b, #0x8 // linked 0x00018e4c
    add	v2.8h, v4.8h, v2.8h // linked 0x00018e50
    dup	v4.8h, v3.h[7] // linked 0x00018e54
    add	v2.8h, v2.8h, v4.8h // linked 0x00018e58
    dup	v4.8h, w6 // linked 0x00018e5c
    cmhs	v3.8h, v4.8h, v3.8h // linked 0x00018e60
    cmhs	v2.8h, v4.8h, v2.8h // linked 0x00018e64
    and	v3.16b, v3.16b, v1.16b // linked 0x00018e68
    sub	v2.8h, v3.8h, v2.8h // linked 0x00018e6c
    addv	h2, v2.8h // linked 0x00018e70
    fmov	w6, s2 // linked 0x00018e74
