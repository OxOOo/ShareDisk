#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>

using namespace std;

typedef shared_ptr<vector<uint8_t> > data_t; // 用来储存数据

data_t CreateData();
data_t CreateData(const string& data);
data_t CreateData(void* data, size_t size);
data_t Clone(data_t data);
data_t Concat(data_t a, data_t b);

// 密钥
struct SecretKey
{
    uint8_t key[32];
};

SecretKey string2secret(string secret_key);

#endif // _COMMON_H_
