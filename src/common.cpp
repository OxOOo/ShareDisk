#include "common.h"
#include <cstring>

using namespace std;

data_t CreateData()
{
    return data_t(new vector<uint8_t>());
}

data_t CreateData(const string& data)
{
    data_t ret = CreateData();
    ret->resize(data.length());
    memcpy(ret->data(), data.c_str(), data.length());
    return ret;
}

data_t CreateData(void* data, size_t size)
{
    data_t ret = CreateData();
    ret->resize(size);
    memcpy(ret->data(), data, size);
    return ret;
}

data_t Clone(data_t data)
{
    return CreateData(data->data(), data->size());
}

data_t Concat(data_t a, data_t b)
{
    data_t ret = CreateData();
    ret->resize(a->size() + b->size());
    memcpy(ret->data(), a->data(), a->size());
    memcpy(ret->data()+a->size(), b->data(), b->size());
    return ret;
}

SecretKey string2secret(string secret_key)
{
    SecretKey ans;
    memset(&ans, 0, sizeof(ans));

    while(secret_key.length() < sizeof(ans)) secret_key += secret_key;
    for(int i = 0; i < (int)secret_key.length(); i ++) {
        ans.key[i%sizeof(ans)] ^= (uint8_t)secret_key[i];
    }

    return ans;
}
