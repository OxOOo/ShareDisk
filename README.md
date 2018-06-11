## 环境相关

* Fuse 3.2.3
* Ubuntu 16.04

## 安装&运行

### 编译

1. 按照`https://github.com/libfuse/libfuse`上的内容安装`libfuse`。

2. 在项目目录中运行`git submodule update --init --recursive`安装子模块

3. 运行`make`编译文件

### 运行

首先需要准备4个目录：

```sh
> mkdir mount mount2 real real2
```

然后可以运行我们的项目：

```sh
> make run
> make run2
```

此时，已经将我们的项目挂在到了`mount`和`mount2`上，`mount`和`mount2`中各有一个子目录，该子目录中的内容将会被加密并同步。