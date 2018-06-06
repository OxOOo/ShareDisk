#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>

using namespace std;

#define MAX_CHUNK_SIZE (4*1024) // 最大数据块大小

typedef shared_ptr<vector<uint8_t> > data_t; // 用来储存数据

data_t CreateData();
data_t CreateData(const string& data);
data_t Concat(data_t a, data_t b);

// 密钥
struct SecretKey
{
    uint8_t key[32];
};

SecretKey string2secret(string secret_key);

#endif // _COMMON_H_
