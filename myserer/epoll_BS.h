#ifndef _EPOLL_BS_H
#define _EPOLL_BS_H
//这个就是epoll多路IO转接封装了以下代码
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#define MAXSIZE 2048
//防止隐式声明waring
int init_listen_fd(int port, int epfd);
void epoll_run(int port);
void do_accept(int lfd, int epfd);
void do_read(int cfd, int epfd);
int get_line(int sock, char *buf, int size);
void disconnect(int cfd, int epfd);
void http_request(const char* request, int cfd);
void send_respond_head(int cfd, int no, const char* disp, const char* type, long len);
void send_file(int cfd, const char* filename);
void send_dir(int cfd, const char* dirname);
void encode_str(char* to, int tosize, const char* from);
void decode_str(char *to, char *from);
const char *get_file_type(const char *name);
#endif
