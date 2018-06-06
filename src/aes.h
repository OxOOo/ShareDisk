// https://github.com/dhuertas/AES
#ifndef _AES_H_
#define _AES_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// AES加密/解密
// key必须为256位(keylen=32)
// inlen必须为16的倍数
// out的空间必须和in的空间一样大

void aes_encode(const uint8_t* key, size_t keylen, const uint8_t* in, size_t inlen, uint8_t* out);
void aes_decode(const uint8_t* key, size_t keylen, const uint8_t* in, size_t inlen, uint8_t* out);

#endif // _AES_H_
