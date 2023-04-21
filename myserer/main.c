#include "epoll_BS.h"
//简单处理语法和修改工作目录，开启epoll监听
int main(int argc,char *argv[]){
	if(argc != 3)
	{
		printf("./server port dir\n");
		exit(1);
	}
	int ret = chdir(argv[2]);
	if(ret == -1)
    {
        perror("chdir error");
        exit(1);
    }
	int port = atoi(argv[1]);
	epoll_run(port);	
	return 0;
}

