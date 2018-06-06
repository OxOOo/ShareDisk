#include <plog/Log.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include "networking.h"

using namespace std;

int main(int argc, char* argv[])
{
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    plog::init(plog::debug, &consoleAppender);

    LOG_INFO << "ShareDisk Start";

    SecretKey key = string2secret("abcdef");
    vector<SecretKey> keys; keys.push_back(key);

    Networking net = Networking(keys);
    net.Listen();
    net.Broadcast(key, CreateData("hello world"));

    while(true)
    {
        data_t data = net.Recv();
        LOG_INFO << "data : " << data->data();
    }

    return 0;
}
