#include "file_control.h"
#include "aes.h"
#include <plog/Log.h>
#include <cassert>
#include <ctime>
#include <cstring>
#include <set>
#include <chrono>
#include <dirent.h>

using namespace std;

#define ASSERT(expr) { if (!(expr)) { LOG_ERROR << #expr; exit(1); } }

FileControl::FileControl(string pd_path, vector<string> keystrings)
{
    this->pd_path = pd_path;
    this->cfg_filename = PathJoin(pd_path, "cfg");

    for(int i = 0; i < (int)keystrings.size(); i ++) {
        KeyEntry entry;
        
        int pos = -1;
        for(int k = 0; k < (int)keystrings[i].length(); k ++)
            if (keystrings[i][k] == ':')
                pos = k;
        LOG_INFO << keystrings[i] << " " << pos;
        ASSERT(pos >= 0);
        
        entry.name = keystrings[i].substr(0, pos);
        entry.keystring = keystrings[i].substr(pos+1);
        entry.key = string2secret(entry.keystring);

        LOG_INFO << entry.name << " " << entry.keystring;

        keys.push_back(entry);
    }

    vector<SecretKey> secrets;
    for(int i = 0; i < (int)keys.size(); i ++) {
        secrets.push_back(keys[i].key);
    }
    net = new Networking(secrets);
}

FileControl::~FileControl()
{
    delete net;
}

void FileControl::Init()
{
    set<string> dirs;

    {
        struct dirent* ent = NULL;
        DIR *pDir = opendir(pd_path.c_str());
        struct stat s;

        while (NULL != (ent = readdir(pDir)))
        {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;

            lstat(Resolve(ent->d_name).c_str(), &s);
            if (S_ISDIR(s.st_mode)) {
                dirs.insert(ent->d_name);
            }
        }
    }

    for(int i = 0; i < (int)keys.size(); i ++)
        if (dirs.find(keys[i].name) == dirs.end()) {
            mkdir(Resolve(keys[i].name).c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        }
    
    LoadCFG();
    
    net->Listen();
    StartThread();
}

string FileControl::Resolve(const string& path) const
{
    return PathJoin(pd_path, path);
}

bool FileControl::IsAccessible(const char *path) const
{
    if (strlen(path) == 1) return true; // "/"

    string tmp = FirstPath(path);
    for(int i = 0; i < (int)keys.size(); i ++) {
        if (tmp == keys[i].name) return true;
    }
    return false;
}

bool FileControl::IsTopLevel(const char *path) const
{
    string name = path;
    if (name[name.length()-1] != '/') name += "/";
    int count = 0;
    for(int i = 0; i < (int)name.length(); i ++) count += name[i] == '/';
    return count <= 2;
}

vector<string> FileControl::KeyNames() const
{
    vector<string> names;
    for(int i = 0; i < (int)keys.size(); i ++) {
        names.push_back(keys[i].name);
    }
    return names;
}

File* FileControl::FindFile(const char *path)
{
    for(int i = 0; i < (int)files.size(); i ++)
    {
        if (strcmp(path, files[i].filename) == 0)
        {
            return &files[i];
        }
    }

    return NULL;
}

void FileControl::Sync(const char *path)
{
    sync_mutex.lock();
    bool flag = false;
    if (file_cache.find(path) != file_cache.end())
    {
        LOG_INFO << "Sync: " << path;
        if (is_dirty.at(path)) {
            time_t timepoint1 = time(NULL);

            int fd = open(Resolve(path).c_str(), O_WRONLY|O_CREAT, 0666);
            SaveFile(path, fd, file_cache.at(path));
            close(fd);
            is_dirty[path] = false;
            flag = true;

            LOG_INFO << "Sync Time1: " << path << " " << time(NULL)-timepoint1;
        }
        LOG_INFO << "Sync End: " << path;
    }
    sync_mutex.unlock();
    
    if (flag) {
        time_t timepoint2 = time(NULL);
        BroadcastFile(path);
        LOG_INFO << "Sync Time2: " << path << " " << time(NULL)-timepoint2;
    }
}

void FileControl::SyncDir(const char *path)
{
    sync_mutex.lock();
    LOG_INFO << "SyncDir: " << path;
    vector<string> files;
    for(const auto& it : file_cache)
    {
        if (it.first.length() >= strlen(path) && strncmp(it.first.c_str(), path, strlen(path)) == 0)
        {
            files.push_back(it.first);
        }
    }
    sync_mutex.unlock();
    for(const auto& name: files)
    {
        Sync(name.c_str());
    }
}

void FileControl::ClearCache(const char *path)
{
    sync_mutex.lock();
    if (file_cache.find(path) != file_cache.end())
    {
        LOG_INFO << "ClearCache: " << path;
        ASSERT(is_dirty.at(path) == false);
        file_cache.erase(file_cache.find(path));
        last_hit.erase(last_hit.find(path));
        is_dirty.erase(is_dirty.find(path));
        last_modify.erase(last_modify.find(path));
        LOG_INFO << "ClearCache End: " << path;
    }
    sync_mutex.unlock();
}

data_t FileControl::Touch(const char *path, bool readonly)
{
    sync_mutex.lock();
    LOG_INFO << "Touch: " << path;
    
    if (file_cache.find(path) == file_cache.end())
    {
        file_cache[path] = LoadFile(path);
        last_modify[path] = 0;
        is_dirty[path] = false;
    }
    last_hit[path] = time(NULL);
    is_dirty[path] |= !readonly;

    LOG_INFO << "Touch End: " << path;
    sync_mutex.unlock();
    return file_cache.at(path);
}

int FileControl::NewFile(const char *path, int flags, mode_t mode)
{
    LOG_INFO << "NewFile: " << path;

    File* x = FindFile(path);
    ASSERT(x == NULL || x->is_deleted);

	int res = open(Resolve(path).c_str(), flags, mode);
    if (res == -1) return res;
    fsync(res);

    if (x == NULL) {
        File file;
        strncpy(file.filename, path, FILENAME_MAX_SIZE);
        files.push_back(file);
        x = &files[files.size()-1];
    }
    x->timestamp = time(NULL);
    x->is_deleted = false;
    x->extra_length = 0;

    SaveCFG();
    BroadcastFile(path);

    return res;
}

int FileControl::ReadFile(const char *path, int fd, char *buf, size_t size, off_t offset)
{
    LOG_INFO << "ReadFile: " << path;
    File *x = FindFile(path);
    ASSERT(x != NULL);

    data_t decoded_data = Touch(path);
    if (decoded_data == nullptr) return -1;

    size_t file_size = decoded_data->size()-x->extra_length;
    offset = min((size_t)offset, file_size);
    size = min(size, file_size-offset);
    memcpy(buf, decoded_data->data()+offset, size);
    
    LOG_INFO << "ReadFile[4]";

    return size;
}

int FileControl::WriteFile(const char *path, int fd, const char *buf, size_t size, off_t offset)
{
    LOG_INFO << "WriteFile: " << path;
    File* x = FindFile(path);
    ASSERT(x != NULL);

    data_t decoded_data = Touch(path, false);
    size_t file_size = decoded_data->size()-x->extra_length;
    file_size = max(file_size, offset + size);
    decoded_data->resize((file_size+15)/16*16);

    memcpy(decoded_data->data()+offset, buf, size);

    x->extra_length = decoded_data->size() - file_size;
    // if (SaveFile(path, fd, decoded_data) == -1) return -1;
    x->is_deleted = false;
    x->timestamp = time(NULL);

    SaveCFG();
    // BroadcastFile(path);

    return size;
}

int FileControl::DeleteFile(const char *path)
{
    LOG_INFO << "DeleteFile: " << path;
    File* x = FindFile(path);
    ASSERT(x != NULL);

    int res = unlink(Resolve(path).c_str());
    if (res == -1) {
        LOG_ERROR << "unlink : " << res << " " << strerror(errno);
        return res;
    }

    x->timestamp = time(NULL);
    x->is_deleted = true;

    SaveCFG();
    BroadcastFile(path);

    return res;
}

int FileControl::RenameFile(const char *from, const char *to)
{
    LOG_INFO << "RenameFile: " << from << " " << to;

    File* x = FindFile(from);
    if (x == NULL) return -EACCES;
    // if (FindFile(to) != NULL) return -EACCES;

    int res = rename(Resolve(from).c_str(), Resolve(to).c_str());
    if (res == -1) {
        LOG_ERROR << "rename : " << res << " " << strerror(errno);
        return res;
    }

    x->timestamp = time(NULL);
    x->is_deleted = true;
    
    File* y = FindFile(to);
    if (y == NULL) {
        File file;
        strncpy(file.filename, to, FILENAME_MAX_SIZE);
        files.push_back(file);
        y = &files[files.size()-1];
    }
    y->timestamp = time(NULL);
    y->is_deleted = false;
    y->extra_length = x->extra_length;
    memcpy(y->extra_data, x->extra_data, 16);

    SaveCFG();
    BroadcastFile(from);
    BroadcastFile(to);

    return res;
}

data_t FileControl::LoadFile(const char* path)
{
    LOG_INFO << "LoadFile: " << path;

    const File* x = FindFile(path);
    //if (x == NULL || x->is_deleted) return nullptr;
    ASSERT(x != NULL && !x->is_deleted);

    int key_index = -1;
    for(int i = 0; i < (int)keys.size(); i ++)
        if (keys[i].name == FirstPath(path))
        {
            key_index = i;
        }
    LOG_INFO << "FirstPath(path) = " << FirstPath(path);
    LOG_INFO << "key_index = " << key_index;
    ASSERT(key_index >= 0);

    size_t file_size = FileSize(Resolve(path));
    if (file_size == 0) return CreateData();

    data_t file_data = CreateData();
    data_t decoded_data = CreateData();
    file_data->resize(file_size+x->extra_length);
    decoded_data->resize(file_size+x->extra_length);
    LOG_INFO << file_size << " " << x->extra_length;
    ASSERT(file_data->size()%16 == 0);

    int rfd = open(Resolve(path).c_str(), O_RDONLY);
    int res = pread(rfd, file_data->data(), file_size, 0);
    if (res == -1) {
        LOG_ERROR << "pread : " << res << " " << strerror(errno);
        return nullptr;
    }
    close(rfd);

    memcpy(file_data->data()+file_size, x->extra_data, x->extra_length);
    aes_decode((uint8_t*)&keys[key_index].key, sizeof(keys[key_index].key), file_data->data(), file_data->size(), decoded_data->data());

    return decoded_data;
}

int FileControl::SaveFile(const char* path, int fd, data_t decoded_data)
{
    LOG_INFO << "SaveFile: " << path << " " << decoded_data->size();

    File* x = FindFile(path);
    ASSERT(x != NULL && !x->is_deleted);
    ASSERT(decoded_data->size() % 16 == 0);
    ASSERT(decoded_data->size() >= x->extra_length);

    int key_index = -1;
    for(int i = 0; i < (int)keys.size(); i ++)
        if (keys[i].name == FirstPath(path))
        {
            key_index = i;
        }
    LOG_INFO << "FirstPath(path) = " << FirstPath(path);
    LOG_INFO << "key_index = " << key_index;
    ASSERT(key_index >= 0);

    LOG_INFO << decoded_data->size() << " " << x->extra_length;

    data_t file_data = CreateData();
    file_data->resize(decoded_data->size());
    
    aes_encode((uint8_t*)&keys[key_index].key, sizeof(keys[key_index].key), decoded_data->data(), decoded_data->size(), file_data->data());

    int res = pwrite(fd, file_data->data(), file_data->size()-x->extra_length, 0);
    if (res == -1) {
        LOG_ERROR << "pwrite : " << res << " " << strerror(errno);
        return res;
    }
    res = ftruncate(fd, file_data->size()-x->extra_length);
    if (res == -1) {
        LOG_ERROR << "ftruncate : " << res << " " << strerror(errno);
        return res;
    }
    fsync(fd);
    memcpy(x->extra_data, file_data->data()+(file_data->size()-x->extra_length), x->extra_length);

    return res;
}

string FileControl::PathJoin(string A, string B) const
{
    if (A[A.length()-1] == '/') A = A.substr(0, A.length()-1);
    if (B.length() > 0 && B[0] == '/') B = B.substr(1);
    return A + "/" + B;
}

string FileControl::FirstPath(const string& path) const
{
    int pos = 1;
    while(pos < path.length() && path[pos] != '/') pos ++;
    return path.substr(1, pos-1);
}

size_t FileControl::FileSize(const string& filepath) const
{
    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) return 0;
    return st.st_size;
}

void FileControl::LoadCFG()
{
    files.clear();

    FILE *fd = fopen(cfg_filename.c_str(), "rb");
    if (!fd) return;
    
    File file;
    while(fread(&file, sizeof(File), 1, fd) > 0)
    {
        files.push_back(file);
    }

    fclose(fd);
}

void FileControl::SaveCFG()
{
    FILE *fd = fopen(cfg_filename.c_str(), "wb");

    for(int i = 0; i < (int)files.size(); i ++)
    {
        fwrite(&files[i], sizeof(File), 1, fd);
    }

    fclose(fd);
}

void debug(data_t data, int pos = 0)
{
    for(int i = pos; i < (int)data->size(); i ++) {
        LOG_INFO << (int32_t)data->at(i);
    }
}

void FileControl::StartThread()
{
    recv_thread = thread([this]() {
        { // online
            PacketHead head;
            head.type = packet_type_online;
            head.time = time(NULL);
            for(int i = 0; i < (int)keys.size(); i ++) {
                memset(head.filename, '\0', FILENAME_MAX_SIZE);
                strncpy(head.filename, keys[i].name.c_str(), FILENAME_MAX_SIZE);
                net->Broadcast(keys[i].key, CreateData(&head, sizeof(head)));
            }
        }
        while(true)
        {
            data_t data = net->Recv();
            if (data->size() < sizeof(PacketHead)) {
                LOG_ERROR << "packet size too small";
                continue;
            }
            PacketHead head = *(PacketHead*)data->data();
            head.filename[FILENAME_MAX_SIZE-1] = '\0';
            if (head.type == packet_type_online) {
                LOG_INFO << "packet_type_online";
                for(int i = 0; i < (int)files.size(); i ++) {
                    if (FirstPath(files[i].filename) == head.filename) {
                        BroadcastFile(files[i].filename);
                    }
                }
            } else if (head.type == packet_type_modify) {
                LOG_INFO << "packet_type_modify " << head.filename;
                ModifyPacket modify = *(ModifyPacket*)(data->data()+sizeof(PacketHead));
                if (data->size() != sizeof(PacketHead)+sizeof(ModifyPacket)+modify.payload_size) {
                    LOG_ERROR << "modify packet size unmatch";
                    continue;
                }

                File* x = FindFile(head.filename);
                if (x != NULL && x->timestamp > head.time) continue;
                if (x == NULL) {
                    File file;
                    memcpy(file.filename, head.filename, FILENAME_MAX_SIZE);
                    files.push_back(file);
                    x = &files[files.size()-1];

                    int fd = open(Resolve(file.filename).c_str(), O_WRONLY|O_CREAT, 0666);
                    fsync(fd);
                    close(fd);
                }
                x->is_deleted = false;

                data_t decoded_data = Touch(x->filename, false);
                ASSERT(decoded_data != nullptr);
                decoded_data->resize(modify.total_size);
                ASSERT(decoded_data->size() % 16 == 0);
                memcpy(decoded_data->data()+modify.payload_offset, data->data()+sizeof(PacketHead)+sizeof(ModifyPacket), modify.payload_size);

                x->extra_length = modify.total_size - modify.file_size;
                x->timestamp = head.time;
                
                SaveCFG();
            } else if (head.type == packet_type_delete) {
                LOG_INFO << "packet_type_delete " << head.filename;
                File* x = FindFile(head.filename);
                if (x == NULL || x->timestamp > head.time) continue;
                Sync(head.filename);
                ClearCache(head.filename);
                if (x->is_deleted == false) {
                    unlink(Resolve(x->filename).c_str());
                }
                x->timestamp = head.time;
                x->is_deleted = true;
                SaveCFG();
            } else {
                LOG_ERROR << "unknow packet type";
            }
        }
    });

    sync_thread = thread([this]() {
        while(true) {

            { // need sync
                sync_mutex.lock();
                vector<string> files;
                for(auto it: file_cache) {
                    if (is_dirty.at(it.first) && last_modify.at(it.first)+2<time(NULL)) { // 距上次修改超过2秒钟则同步
                        files.push_back(it.first);
                    }
                }
                sync_mutex.unlock();
                for(const auto& name: files) {
                    Sync(name.c_str());
                }
            }
            
            { // need free
                sync_mutex.lock();
                vector<string> files;
                for(auto it: file_cache) {
                    if (last_hit.at(it.first)+30<time(NULL)) { // 距上次访问超过30秒钟则释放内存
                        files.push_back(it.first);
                    }
                }
                sync_mutex.unlock();
                for(const auto& name: files) {
                    ClearCache(name.c_str());
                }
            }

            this_thread::sleep_for(chrono::seconds(1));
        }
    });
}

void FileControl::BroadcastFile(const char* path)
{
    File* x = FindFile(path);
    ASSERT(x != NULL);
    
    int key_index = -1;
    for(int i = 0; i < (int)keys.size(); i ++)
        if (FirstPath(path) == keys[i].name) {
            key_index = i;
        }
    ASSERT(key_index >= 0);

    if (x->is_deleted) {
        LOG_INFO << "send delete " << x->filename;
        PacketHead head;
        head.type = packet_type_delete;
        head.time = x->timestamp-1;
        memcpy(head.filename, x->filename, FILENAME_MAX_SIZE);
        net->Broadcast(keys[key_index].key, CreateData(&head, sizeof(head)));
    } else {
        data_t decoded_data = Touch(path);
        LOG_INFO << "send modify " << x->filename << " " << decoded_data->size() << " " << x->extra_length;
        for(size_t pos = 0; pos == 0 || pos < decoded_data->size(); pos += CHUNK_MAX_SIZE) {
            int size = min((int)CHUNK_MAX_SIZE, (int)decoded_data->size() - (int)pos);

            PacketHead head;
            head.type = packet_type_modify;
            head.time = x->timestamp-1;
            memcpy(head.filename, x->filename, FILENAME_MAX_SIZE);
            ModifyPacket modify;
            modify.file_size = decoded_data->size()-x->extra_length;
            modify.total_size = decoded_data->size();
            modify.payload_offset = pos;
            modify.payload_size = size;

            data_t data = CreateData();
            data->resize(sizeof(head)+sizeof(modify)+size);
            memcpy(data->data(), &head, sizeof(head));
            memcpy(data->data()+sizeof(head), &modify, sizeof(modify));
            memcpy(data->data()+sizeof(head)+sizeof(modify), decoded_data->data()+pos, size);

            net->Broadcast(keys[key_index].key, data);
        }
    }
}
