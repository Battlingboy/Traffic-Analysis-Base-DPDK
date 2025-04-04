#include <lookup3.h>

#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

// 混合3个32-bit的值
#define lookup_mix(a, b, c) { \
    a -= c;  a ^= rot(c, 4);  c += b; \
    b -= a;  b ^= rot(a, 6);  a += c; \
    c -= b;  c ^= rot(b, 8);  b += a; \
    a -= c;  a ^= rot(c,16);  c += b; \
    b -= a;  b ^= rot(a,19);  a += c; \
    c -= b;  c ^= rot(b, 4);  b += a; \
}

// 最终将3个32位数值(a,b,c)混合到c中
#define lookup_final(a, b, c) { \
    c ^= b; c -= rot(b,14); \
    a ^= c; a -= rot(c,11); \
    b ^= a; b -= rot(a,25); \
    c ^= b; c -= rot(b,16); \
    a ^= c; a -= rot(c,4);  \
    b ^= a; b -= rot(a,14); \
    c ^= b; c -= rot(b,24); \
}


/*******************************************************************************
* Description: lookup3 hash函数，返回hash后的值
* Parameters：
*   @key：要hash的key，这里是IPv4的5元组（或四元组）
*   @length：key的长度，这里是IPv4的5元组（或四元组）的长度，以字节为单位
*   @init_val：hash的初始值
* Return: hash后的值
********************************************************************************/
uint32_t lookup3_hash(const void *key, uint32_t length, uint32_t initval) {
    const uint32_t *k = (const uint32_t *)key;

    uint32_t a, b, c;

    a = b = c = 0xdeadbeef + (length << 2) + initval;   // 初始化a,b,c

    while (length > 12) {
        a += k[0];
        b += k[1];
        c += k[2];
        lookup_mix(a, b, c);
        length -= 12;
        k += 3;
    }

    switch (length) {
        case 12: 
            c += k[2]; 
            b += k[1]; 
            a += k[0]; 
            break;
        case 11: 
            c += k[2]&0xffffff; 
            b += k[1]; 
            a += k[0]; 
            break;
        case 10: 
            c += k[2]&0xffff; 
            b += k[1]; 
            a += k[0]; 
            break;
        case 9 : 
            c += k[2]&0xff; 
            b += k[1]; 
            a += k[0]; 
            break;
        case 8 : 
            b+=k[1]; 
            a+=k[0]; 
            break;
        case 7 : 
            b += k[1] & 0xffffff; 
            a += k[0]; 
            break;
        case 6 : 
            b += k[1]&0xffff; 
            a += k[0]; 
            break;
        case 5 : 
            b += k[1]&0xff; 
            a += k[0]; 
            break;
        case 4 : 
            a += k[0]; 
            break;
        case 3 : 
            a += k[0] & 0xffffff; 
            break;
        case 2 : 
            a += k[0] & 0xffff; 
            break;
        case 1 : 
            a += k[0] & 0xff; 
            break;
        case 0 : { return c; }              /* zero length strings require no mixing */
    }

    lookup_final(a, b, c);

    return c;
}