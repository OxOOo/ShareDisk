// static int remove_dir(const char *dir)
// {
//     char cur_dir[] = ".";
//     char up_dir[] = "..";
//     char dir_name[128];
//     DIR *dirp;
//     struct dirent *dp;
//     struct stat dir_stat;

//     if ( 0 != access(dir, F_OK) ) {
//         return 0;
//     }

//     if ( 0 > lstat(dir, &dir_stat) ) {
//         perror("get directory stat error");
//         return -1;
//     }

//     if ( S_ISREG(dir_stat.st_mode) ) {  
//         remove(dir);
//     } else if ( S_ISDIR(dir_stat.st_mode) ) {   
//         dirp = opendir(dir);
//         while ( (dp=readdir(dirp)) != NULL ) {
//             if ( (0 == strcmp(cur_dir, dp->d_name)) || (0 == strcmp(up_dir, dp->d_name)) ) {
//                 continue;
//             }

//             sprintf(dir_name, "%s/%s", dir, dp->d_name);
//             remove_dir(dir_name);  
//         }
//         closedir(dirp);

//         rmdir(dir);     
//     } else {
//         perror("unknow file type!");    
//     }
//     return 0;
// }

// static void getnewpath(char *tmp, const char *path, char *pd) {
// 	int a1 = strlen(path);
// 	int a2 = strlen(pd);
// 	memset(tmp , 0, a1+a2+1);
// 	for(int i =0; i<a1+a2; i++)
// 		if (i < a2) tmp[i] = pd[i];
// 		else tmp[i] = path[i-a2];
// }

// static int is_accessible(const char *path)
// {
// 	int i, j, k, l;
// 	l = strlen(path);
// 	if (l == 1)	return 1;
// 	char *tmp = (char *)malloc(sizeof(char) * (l + 1));
// 	for (i = 0, j = 0; i < l; i++, j++)
// 	{
// 		if (i == 0 && path[i] == '/')
// 		{
// 			j--;
// 			continue;
// 		}
// 		if (path[i] == '/')	break;
// 		else	tmp[j] = path[i];
// 	}
// 	tmp[j] = '\0';
// 	for (k = 0, i = 0; i < key_count; i++)
// 		if (strcmp(key_list[i], tmp) == 0)
// 		{
// 			k = 1;
// 			break;
// 		}
// 	return k;
// }

// static int is_key_directory(const char *path)
// {
// 	int i, k, l;
// 	l = strlen(path);
// 	for (i = 1, k = 0; i < l; i++)
// 	{
// 		if (path[i] == '/') k ++;
// 		if (k >= 1 && path[i] != '\0')	return 1;
// 	}
// 	return 0;
// }