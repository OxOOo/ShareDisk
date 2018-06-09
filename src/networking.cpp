#include "networking.h"
#include "aes.h"
#include <plog/Log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <ctime>

Networking::Networking(const vector<SecretKey>& keys)
{
    this->keys = keys;
    this->listen_fd = -1;

    assert(sizeof(MessageHead) % 16 == 0);
}

bool Networking::Listen()
{
    LOG_INFO << "Trying to Listen";

    listen_fd = socket(AF_INET, SOCK_DGRAM, 0); //AF_INET:IPV4;SOCK_DGRAM:UDP
    if(listen_fd < 0)
    {
        perror("create socket fail:");
        return false;
    }

    int enable=1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) < 0)
    {
        perror("enable broadcast fail:");
        return false;
    }

    struct sockaddr_in listen_addr;
    bool bind_success = false;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY); //IP地址，需要进行网络序转换，INADDR_ANY：本地地址

    for(int port = UDP_PORT_START; port <= UDP_PORT_END; port ++)
    {
        listen_addr.sin_port = htons(port); //端口号，需要网络序转换

        int ret = bind(listen_fd, (struct sockaddr*)&listen_addr, sizeof(listen_addr));
        if (ret >= 0)
        {
            LOG_INFO << "Success Listen At Port " << port;
            bind_success = true;
            break;
        }
    }
    if (!bind_success)
    {
        perror("bind port fail:");
        return false;
    }

    return true;
}

data_t Networking::Recv()
{
    data_t packet_data = CreateData();
    socklen_t len;
    int count;
    struct sockaddr_in remote_addr; //remote_addr用于记录发送方的地址信息

    packet_data->resize(1<<16);

    while(1)
    {
        memset(packet_data->data(), 0, packet_data->size());
        len = sizeof(remote_addr);
        count = recvfrom(listen_fd, packet_data->data(), packet_data->size(), 0, (struct sockaddr*)&remote_addr, &len);  //recvfrom是拥塞函数，没有数据就一直拥塞
        if(count == -1)
        {
            perror("recieve data fail:");
            continue;
        }

        // message check
        if (count < sizeof(MessageHead)*2)
        {
            LOG_ERROR << "Message Too Small";
            continue;
        }
        MessageHead head = *(MessageHead*)packet_data->data();
        uint32_t payload_real_length;
        uint32_t payload_total_length;
        if (!CheckHead(head, payload_real_length, payload_total_length))
        {
            LOG_ERROR << "MessageHead Check Fail";
            continue;
        }
        if (payload_total_length % 16 != 0) // AES data size % 16 == 0
        {
            LOG_ERROR << "payload_total_length % 16 = " << payload_total_length % 16 << " != 0";
            continue;
        }
        if (count != sizeof(MessageHead)*2+payload_total_length)
        {
            LOG_ERROR << "payload_total_length = " << payload_total_length << " count = " << count;
            continue;
        }
        int secret_key_index = -1;
        for(int i = 0; i < (int)keys.size(); i ++)
        {
            uint8_t* content = (uint8_t*)&head;
            uint8_t encrypted[sizeof(MessageHead)];
            aes_encode((uint8_t*)&keys[i], sizeof(SecretKey), content, sizeof(MessageHead), encrypted);
            if (memcmp(packet_data->data()+sizeof(MessageHead), encrypted, sizeof(MessageHead)) == 0)
            {
                secret_key_index = i;
                break;
            }
        }
        if (secret_key_index < 0)
        {
            LOG_ERROR << "Can Not Decode Data";
            continue;
        }

        LOG_INFO << "Recv a Packet From " << inet_ntoa(remote_addr.sin_addr) << ":" << ntohs(remote_addr.sin_port);
        
        data_t data = CreateData();
        data->resize(payload_total_length);
        aes_decode((uint8_t*)&keys[secret_key_index], sizeof(SecretKey), packet_data->data()+sizeof(MessageHead)*2, payload_total_length, data->data());
        data->resize(payload_real_length);

        return data;
    }
}

void Networking::Broadcast(const SecretKey& key, data_t _data)
{
    data_t data = Clone(_data);
    data_t packet_data = CreateData();

    const uint32_t payload_real_length = data->size();
    const uint32_t payload_total_length = (payload_real_length + 16 - 1) / 16 * 16;
    const uint32_t size = sizeof(MessageHead)*2+payload_total_length;
    data->resize(payload_total_length);

    packet_data->resize(size);

    MessageHead head = CreateHead(payload_real_length, payload_total_length);
    *(MessageHead*)packet_data->data() = head;
    aes_encode((uint8_t*)&key, sizeof(SecretKey), packet_data->data(), sizeof(MessageHead), packet_data->data()+sizeof(MessageHead));
    aes_encode((uint8_t*)&key, sizeof(SecretKey), data->data(), payload_total_length, packet_data->data()+sizeof(MessageHead)*2);

    for(int port = UDP_PORT_START; port <= UDP_PORT_END; port ++)
    {
        struct sockaddr_in s;
        memset(&s, 0, sizeof(struct sockaddr_in));
        s.sin_family = AF_INET;
        s.sin_port = (in_port_t)htons(port);
        s.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        if(sendto(listen_fd, packet_data->data(), size, 0, (struct sockaddr *)&s, sizeof(struct sockaddr_in)) < 0)
        {
            perror("sendto:");
        }
    }
}

Networking::MessageHead Networking::CreateHead(uint32_t payload_real_length, uint32_t payload_total_length)
{
    MessageHead head;
    head.version = htonl(1);
    head.time = htonl(time(0));
    head.payload_real_length = htonl(payload_real_length);
    head.payload_total_length = htonl(payload_total_length);
    return head;
}

bool Networking::CheckHead(const Networking::MessageHead& head, uint32_t& payload_real_length, uint32_t& payload_total_length)
{
    if (ntohl(head.version) != 1) return false;
    if (ntohl(head.time) < time(0) - 30 || time(0) + 30 < ntohl(head.time)) return false;
    payload_real_length = ntohl(head.payload_real_length);
    payload_total_length = ntohl(head.payload_total_length);
    return true;
}
