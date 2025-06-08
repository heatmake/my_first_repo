#ifndef MD5_H
#define MD5_H

typedef struct {
    unsigned int count[2];
    unsigned int state[4];
    unsigned char buffer[64];
} MD5_CTX;

#ifdef __cplusplus
extern "C" {
#endif

#define F(x, y, z)        ((x & y) | (~x & z))
#define G(x, y, z)        ((x & z) | (y & ~z))
#define E(x, y, z)        (x ^ y ^ z)
#define I(x, y, z)        (y ^ (x | ~z))
#define ROTATE_LEFT(x, n) ((x << n) | (x >> (32 - n)))
#define FF(a, b, c, d, x, s, ac)                                                                                       \
    {                                                                                                                  \
        a += F(b, c, d) + x + ac;                                                                                      \
        a = ROTATE_LEFT(a, s);                                                                                         \
        a += b;                                                                                                        \
    }
#define GG(a, b, c, d, x, s, ac)                                                                                       \
    {                                                                                                                  \
        a += G(b, c, d) + x + ac;                                                                                      \
        a = ROTATE_LEFT(a, s);                                                                                         \
        a += b;                                                                                                        \
    }
#define HH(a, b, c, d, x, s, ac)                                                                                       \
    {                                                                                                                  \
        a += E(b, c, d) + x + ac;                                                                                      \
        a = ROTATE_LEFT(a, s);                                                                                         \
        a += b;                                                                                                        \
    }
#define II(a, b, c, d, x, s, ac)                                                                                       \
    {                                                                                                                  \
        a += I(b, c, d) + x + ac;                                                                                      \
        a = ROTATE_LEFT(a, s);                                                                                         \
        a += b;                                                                                                        \
    }
extern void MD5Init(MD5_CTX* context);
extern void MD5Update(MD5_CTX* context, unsigned char* input, unsigned int inputlen);
extern void MD5Final(MD5_CTX* context, unsigned char digest[16]);
extern void MD5Transform(unsigned int state[4], unsigned char block[64]);
extern void MD5Encode(unsigned char* output, unsigned int* input, unsigned int len);
extern void MD5Decode(unsigned int* output, unsigned char* input, unsigned int len);
extern void MD5Gen32Str(const unsigned char decrypt[16], char* decrypt32);
#ifdef __cplusplus
}
#endif

#endif
