#include "file_control.h"
#include "aes.h"
#include <plog/Log.h>
#include <cassert>
#include <ctime>
#include <cstring>
#include <set>
#include <dirent.h>

using namespace std;

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
        assert(pos >= 0);
        
        entry.name = keystrings[i].substr(0, pos);
        entry.keystring = keystrings[i].substr(pos+1);
        entry.key = string2secret(entry.keystring);

        LOG_INFO << entry.name << " " << entry.keystring;

        keys.push_back(entry);
    }

    LoadCFG();
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

int FileControl::NewFile(const char *path, int flags, mode_t mode)
{
    LOG_INFO << "NewFile: " << path;

    File* x = FindFile(path);
    assert(x == NULL || x->is_deleted);

	int res = open(Resolve(path).c_str(), flags, mode);
    if (res == -1) return res;

    if (x == NULL) {
        File file;
        strncpy(file.filename, path, FILENAME_MAX_SIZE);
        files.push_back(file);
        x = &files[files.size()-1];
    }
    x->timestamp = time(NULL);
    x->is_deleted = true;
    x->extra_length = 0;

    SaveCFG();
    return res;
}

int FileControl::ReadFile(const char *path, int fd, char *buf, size_t size, off_t offset)
{
    LOG_INFO << "ReadFile: " << path;
    File *x = FindFile(path);
    assert(x != NULL);

    int key_index = -1;
    for(int i = 0; i < (int)keys.size(); i ++)
        if (keys[i].name == FirstPath(path))
        {
            key_index = i;
        }
    LOG_INFO << "FirstPath(path) = " << FirstPath(path);
    LOG_INFO << "key_index = " << key_index;
    assert(key_index >= 0);

    data_t file_data = CreateData();
    data_t decoded_data = CreateData();
    size_t file_size = FileSize(Resolve(path));
    file_data->resize(file_size+x->extra_length);
    decoded_data->resize(file_size+x->extra_length);

    LOG_INFO << "ReadFile[0]";

    int res = pread(fd, file_data->data(), file_size, 0);
    if (res == -1) {
        LOG_ERROR << "pread : " << res << " " << strerror(errno);
        return res;
    }

    LOG_INFO << "ReadFile[1]";

    memcpy(file_data->data()+file_size, x->extra_data, x->extra_length);
    LOG_INFO << "ReadFile[2]";
    aes_decode((uint8_t*)&keys[key_index].key, sizeof(keys[key_index].key), file_data->data(), file_data->size(), decoded_data->data());
    LOG_INFO << "ReadFile[3]";
    memcpy(buf, decoded_data->data()+offset, size);
    LOG_INFO << "ReadFile[4]";

    return res;
}

int FileControl::WriteFile(const char *path, int fd, const char *buf, size_t size, off_t offset)
{
    LOG_INFO << "WriteFile: " << path;
    File* x = FindFile(path);
    assert(x != NULL);

    int key_index = -1;
    for(int i = 0; i < (int)keys.size(); i ++)
        if (keys[i].name == FirstPath(path))
        {
            key_index = i;
        }
    LOG_INFO << "FirstPath(path) = " << FirstPath(path);
    LOG_INFO << "key_index = " << key_index;
    assert(key_index >= 0);

    data_t file_data = CreateData();
    data_t decoded_data = CreateData();
    size_t file_size = FileSize(Resolve(path));
    file_data->resize(file_size+x->extra_length);
    decoded_data->resize(file_size+x->extra_length);

    LOG_INFO << "WriteFile[0] " << file_data->size() << " " << file_size << " " << x->extra_length;
    LOG_INFO << offset << " " << size;

    { // read all data
        int rfd = open(Resolve(path).c_str(), O_RDONLY);
        int res = pread(rfd, file_data->data(), file_size, 0);
        if (res == -1) {
            LOG_ERROR << "pread : " << res << " " << strerror(errno);
            return res;
        }
        close(rfd);
    }

    LOG_INFO << "WriteFile[1]";

    memcpy(file_data->data()+file_size, x->extra_data, x->extra_length);
    LOG_INFO << "WriteFile[2]";
    aes_decode((uint8_t*)&keys[key_index].key, sizeof(keys[key_index].key), file_data->data(), file_data->size(), decoded_data->data());
    LOG_INFO << "WriteFile[3]";

    decoded_data->resize(max(file_size, size+offset));
    LOG_INFO << "WriteFile[4]";
    memcpy(decoded_data->data()+offset, buf, size);
    LOG_INFO << "WriteFile[5]";

    size_t extra_length = (decoded_data->size()+15)/16*16 - decoded_data->size();
    size_t total_length = decoded_data->size()+extra_length;
    LOG_INFO << "WriteFile[6]";
    decoded_data->resize(total_length);
    file_data->resize(total_length);
    LOG_INFO << "WriteFile[7]";
    aes_encode((uint8_t*)&keys[key_index].key, sizeof(keys[key_index].key), decoded_data->data(), decoded_data->size(), file_data->data());
    LOG_INFO << "WriteFile[8]";

    int res = pwrite(fd, file_data->data(), max(file_size, size+offset), 0);
    if (res == -1) {
        LOG_ERROR << "pwrite : " << res << " " << strerror(errno);
        return res;
    }
    LOG_INFO << "WriteFile[9]";
    x->timestamp = time(NULL);
    x->extra_length = extra_length;
    memcpy(x->extra_data, file_data->data()+max(file_size, size+offset), extra_length);

    LOG_INFO << max(file_size, size+offset) << " " << max(file_size, size+offset)/1024;

    SaveCFG();
    return res;
}

int FileControl::DeleteFile(const char *path)
{
    LOG_INFO << "DeleteFile: " << path;
    File* x = FindFile(path);
    assert(x != NULL);

    int res = unlink(Resolve(path).c_str());
    if (res == -1) {
        LOG_ERROR << "unlink : " << res << " " << strerror(errno);
        return res;
    }

    x->timestamp = time(NULL);
    x->is_deleted = true;

    SaveCFG();
    return res;
}

int FileControl::RenameFile(const char *from, const char *to)
{
    LOG_INFO << "RenameFile: " << from << " " << to;

    File* x = FindFile(from);
    if (x == NULL) return -EACCES;
    if (FindFile(to) != NULL) return -EACCES;

    int res = rename(Resolve(from).c_str(), Resolve(to).c_str());
    if (res == -1) {
        LOG_ERROR << "rename : " << res << " " << strerror(errno);
        return res;
    }

    strncpy(x->filename, to, FILENAME_MAX_SIZE);

    SaveCFG();
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
    stat(filepath.c_str(), &st);
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
