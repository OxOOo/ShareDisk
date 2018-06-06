#ifndef _NETWORKING_H_
#define _NETWORKING_H_

#include "common.h"
#include <vector>

using namespace std;

#define UDP_PORT_START 7645
#define UDP_PORT_END 7655

#define BUF_SIZE (MAX_CHUNK_SIZE*2)

class Networking
{
public:
    Networking(const vector<SecretKey>& keys);

    bool Listen(); // 监听成功则返回true

    data_t Recv(); // 接收一个数据包，已解密

    void Broadcast(const SecretKey& key, data_t data); // 以密钥key广播数据

private: // MessageHead|encrypted MessageHead|payload
    struct MessageHead
    {
        uint32_t version; // must be 1
        uint32_t time;
        uint32_t payload_real_length;
        uint32_t payload_total_length;
    };
    MessageHead CreateHead(uint32_t payload_real_length, uint32_t payload_total_length);
    bool CheckHead(const MessageHead& head, uint32_t& payload_real_length, uint32_t& payload_total_length);

private:
    vector<SecretKey> keys;
    int listen_fd;
};

#endif // _NETWORKING_H_
