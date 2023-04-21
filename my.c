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

//发送错误消息
void send_error(int cfd, int status, char *title, char *text)
{
	char buf[4096] = {0};

	sprintf(buf, "%s %d %s\r\n", "HTTP/1.1", status, title);
	sprintf(buf+strlen(buf), "Content-Type:%s\r\n", "text/html");
	sprintf(buf+strlen(buf), "Content-Length:%d\r\n", -1);
	sprintf(buf+strlen(buf), "Connection: close\r\n");
	send(cfd, buf, strlen(buf), 0);
	send(cfd, "\r\n", 2, 0);

	memset(buf, 0, sizeof(buf));

	sprintf(buf, "<html><head><title>%d %s</title></head>\n", status, title);
	sprintf(buf+strlen(buf), "<body bgcolor=\"#cc99cc\"><h2 align=\"center\">%d %s</h4>\n", status, title);
	sprintf(buf+strlen(buf), "%s\n", text);
	sprintf(buf+strlen(buf), "<hr>\n</body>\n</html>\n");
	send(cfd, buf, strlen(buf), 0);
	
	return ;
}
// 获取一行 \r\n 结尾的数据 
int get_line(int cfd, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;
    while ((i < size-1) && (c != '\n')) {  
        n = recv(cfd, &c, 1, 0);
        if (n > 0) {     
            if (c == '\r') {            
                n = recv(cfd, &c, 1, MSG_PEEK);//MSG_PEEK:预读入
                if ((n > 0) && (c == '\n')) {              
                    recv(cfd, &c, 1, 0);
                } else {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        } else {      
            c = '\n';
        }
    }
    buf[i] = '\0';
    if (-1 == n)
    	i = n;
    return i;
}
int init_listen_fd(int port,int epfd){
	//创建监听socket
	int lfd = socket(AF_INET,SOCK_STREAM,0);
	if(lfd == -1) {  
        perror("socket error");
        exit(1);
    }
	//创建服务器地址结构
	struct sockaddr_in seraddr;
	//bzero(&seraddr,sizeof(seraddr));
	memset(&seraddr, 0, sizeof(seraddr));
	seraddr.sin_family = AF_INET;
	seraddr.sin_port = htons(port);
	seraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	//端口复用
	int opt = 1;
	setsockopt(lfd ,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
	//给lfd绑定地质结构
	int ret = bind(lfd,(struct sockaddr*)&seraddr,sizeof(seraddr));
	if(ret == -1) {    
        perror("bind error");
        exit(1);
    }
	//设置监听上限
	ret = listen(lfd,128);
	if(ret == -1) {    
        perror("listen error");
        exit(1);
    }
	//将lfd添加到epoll树上	
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd;
	ret = epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);
	if(ret == -1) {  
        perror("epoll_ctl add lfd error");
        exit(1);
    }
	//返回lfd
	return lfd;
}
void do_accept(int lfd,int epfd){
	//接受cfd
	struct sockaddr_in cliaddr;
	socklen_t cliaddr_len = sizeof(cliaddr);
	int cfd = accept(lfd,(struct sockaddr*)&cliaddr,&cliaddr_len);
	if(cfd == -1) {  
        perror("accept error");
        exit(1);
    }
	//打印客户端IP+端口
	char client_ip[64] = {0};
	printf("NEW CLIENT IP: %s,port: %d,cfd = %d\n",inet_ntop(AF_INET,&cliaddr.sin_addr.s_addr,client_ip,sizeof(client_ip)),ntohs(cliaddr.sin_port),cfd);
	//设置cfd非阻塞
	int flag = fcntl(cfd,F_GETFL);
	flag |= O_NONBLOCK;
	fcntl(cfd, F_SETFL, flag);
	//将cfd添加到epoll监听树上
	struct epoll_event ev;
	ev.data.fd = cfd;
	//设置边沿非阻塞模式
	ev.events = EPOLLIN | EPOLLET;
	int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
	if(ret == -1) { 
        perror("epoll_ctl add cfd error");
        exit(1);
    }
}

void disconnect(int cfd,int epfd){
	int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
	if(ret == -1) {   
        perror("epoll_ctl del cfd error");
        exit(1);
    }
	close(cfd);
}
void send_respond_head(int cfd,int no,const char *disp,const char *type,long len){
	char buf[1024] = {0};
	sprintf(buf, "HTTP/1.1 %d %s\r\n", no, disp);
	send(cfd, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: %s\r\n", type);
	sprintf(buf+strlen(buf), "Content-Length:%ld\r\n", len);
	send(cfd, buf, strlen(buf), 0);
	send(cfd, "\r\n", 2, 0);
}
void send_file(int cfd,const char *file){
	int n = 0, ret;
	char buf[4096] = {0};
	
	// 打开的服务器本地文件。  --- cfd 能访问客户端的 socket
	int fd = open(file, O_RDONLY);
	if(fd == -1) {   
        send_error(cfd, 404, "Not Found", "NO such file or direntry");
        exit(1);
    }
	
	while ((n = read(fd, buf, sizeof(buf))) > 0) {		
		ret = send(cfd, buf, n, 0);
		if (ret == -1) {
			if(errno == EAGAIN){ //以非阻塞方式读数据，但没有，需要再次读
				printf("-------------------------EAGAIN\n");
				continue;
			}else if(errno == EINTR){//Interrupted function call中断了
				printf("-------------------------EINTR\n");
				continue;
			}else{
				printf("send error\n");
				exit(1);	
			}
		}
		}
		if(n == -1){
			perror("read file error");
			exit(1);
	}
	close(fd);	
}
// 发送目录内容
void send_dir(int cfd, const char* dirname)
{
    int i, ret;

    // 拼一个html页面<table></table>
    char buf[4094] = {0};

    sprintf(buf, "<html><head><title>目录名: %s</title></head>", dirname);
    sprintf(buf+strlen(buf), "<body><h1>当前目录: %s</h1><table>", dirname);

    char enstr[1024] = {0};
    char path[1024] = {0};
    
    // 目录项二级指针
    struct dirent** ptr;
    int num = scandir(dirname, &ptr, NULL, alphasort);
    
    // 遍历
    for(i = 0; i < num; ++i) {
        char* name = ptr[i]->d_name;
        // 拼接文件的完整路径
        sprintf(path, "%s/%s", dirname, name);
        printf("path = %s ===================\n", path);
        struct stat st;
        stat(path, &st);

		// 编码生成 %E5 %A7 之类的东西
        encode_str(enstr, sizeof(enstr), name);
        
        // 如果是文件
        if(S_ISREG(st.st_mode)) {       
            sprintf(buf+strlen(buf), 
                    "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        } else if(S_ISDIR(st.st_mode)) {		// 如果是目录       
            sprintf(buf+strlen(buf), 
                    "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        ret = send(cfd, buf, strlen(buf), 0);
        if (ret == -1) {
            if (errno == EAGAIN) {
                perror("send error:");
                continue;
            } else if (errno == EINTR) {
                perror("send error:");
                continue;
            } else {
                perror("send error:");
                exit(1);
            }
        }
        memset(buf, 0, sizeof(buf));
    }
	// 字符串拼接
    sprintf(buf+strlen(buf), "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);

    printf("dir message send OK!!!!\n");
#if 0
    // 打开目录
    DIR* dir = opendir(dirname);
    if(dir == NULL)
    {
        perror("opendir error");
        exit(1);
    }

    // 读目录
    struct dirent* ptr = NULL;
    while( (ptr = readdir(dir)) != NULL )
    {
        char* name = ptr->d_name;
    }
    closedir(dir);
#endif
}
// 通过文件名获取文件的类型
const char *get_file_type(const char *name)
{
    char* dot;

    // 自右向左查找‘.’字符, 如不存在返回NULL
    dot = strrchr(name, '.');   
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp( dot, ".wav" ) == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}
//处理http请求，判断文件是否存在，回发
void http_request(const char* request, int cfd){
	char method[16],path[256],protocol[16];
	sscanf(request,"%[^ ] %[^ ] %[^ ]",method,path,protocol);
	printf("method = %s,path = %s,protocol = %s\n",method,path,protocol);
	
	//解码中文乱码-》中文
	decode_str(path,path);
	char* file = path+1; //取出访问的文件名
	if(strcmp(path, "/") == 0) {    
        // file的值, 资源目录的当前位置
        file = "./";
    }
	//获取文件属性	
	struct stat st;
	int ret = stat(file,&st);
	if(ret == -1) { 
        send_error(cfd, 404, "Not Found", "NO such file or direntry");     
        return;
    }
	//判断是目录还是文件
	if(S_ISREG(st.st_mode)){//是普通文件
		send_respond_head(cfd,200,"OK",get_file_type(file),-1);
		printf("open filename：%s\n",file);
		send_file(cfd,file);
	}else if(S_ISDIR(st.st_mode)){//是目录
		send_respond_head(cfd,200,"OK",get_file_type(".html"),st.st_size);
		send_dir(cfd,file);
	}
	
}
void do_read(int cfd,int epfd){
	//以前：
	//小到大 write写回
	//现在：
	//读取一行http协议， 拆分， 获取 get 文件名 协议号
	char buf[1024] = {0};
	int len = get_line(cfd,buf,sizeof(buf));
	if(len == 0){
		printf("客户端断开连接\n");
		disconnect(cfd,epfd);
	} else{//len>0
		printf("--------------请求头---------------\n");
		printf("请求数据：%s",buf);	
		while(1){
			char line[1024] = {0};
			len = get_line(cfd,line,sizeof(line));
			if(line[0] == '\n'){
				break;
			}else if(len == -1)
				break;
			else{	
				//printf("%s",line);
			}
		}
		printf("----------------------The End----------------\n");
	}
	//判断get请求
	if(strncasecmp("get",buf,3) == 0){
		// 处理http请求
		http_request(buf,cfd);
		disconnect(cfd,epfd);
	}
}
void epoll_run(int port){
	//创建一个epoll树根
	int epfd = epoll_create(MAXSIZE);
	if(epfd == -1) {   
        perror("epoll_create error");
        exit(1);
    }
	//创建lfd，并添加至树上
	int lfd = init_listen_fd(port,epfd);	
	//监听接节点对应事件
	struct epoll_event all_events[MAXSIZE];
	while(1){
		int ret = epoll_wait(epfd , all_events, MAXSIZE, 0);//all_events：传出的触发事件 -1：阻塞监听 | 也许是因为浏览器访问ico和数据，如果-1就调用不到目录！！！
		if(ret == -1) {
			perror("epoll_wait error");
            exit(1);
		}
            
	
		for(int i = 0; i < ret; i++ ){
			if(!(all_events[i].events & EPOLLIN))//非读事件
				continue;
			if(all_events[i].data.fd == lfd)//连接事件
				do_accept(lfd,epfd);
			else						//读请求
			{
				printf("======================before do read, ret = %d\n", ret);
				do_read(all_events[i].data.fd,epfd);
				printf("=========================================after do read\n");
			}
			
		}
	}
}	
// 16进制数转化为10进制
int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}

/*
 *  这里的内容是处理%20之类的东西！是"解码"过程。
 *  %20 URL编码中的‘ ’(space)
 *  %21 '!' %22 '"' %23 '#' %24 '$'
 *  %25 '%' %26 '&' %27 ''' %28 '('......
 *  相关知识html中的‘ ’(space)是&nbsp
 */
void encode_str(char* to, int tosize, const char* from)
{
    int tolen;

    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from) {    
        if (isalnum(*from) || strchr("/_.-~", *from) != (char*)0) {      
            *to = *from;
            ++to;
            ++tolen;
        } else {
            sprintf(to, "%%%02x", (int) *from & 0xff);
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}

void decode_str(char *to, char *from)
{
    for ( ; *from != '\0'; ++to, ++from  ) {     
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2])) {       
            *to = hexit(from[1])*16 + hexit(from[2]);
            from += 2;                      
        } else {
            *to = *from;
        }
    }
    *to = '\0';
}

//简单处理语法和修改工作目录，开启epoll监听
int main(int argc,char *argv[]){
	if(argc != 3)
	{
		printf("./xxx port dir\n");
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