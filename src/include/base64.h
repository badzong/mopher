#ifndef _BASE64_H_
#define _BASE64_H_

int base64_encode(char *dest, int size, unsigned char *src, int slen);
char *base64_enc_malloc(unsigned char *src, int slen);
int base64_decode(unsigned char *dest, int size, char *src);
unsigned char *base64_dec_malloc(char *src, int *size);
int base64_init(void);
void base64_test(int n);

#endif
