#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
 
#include "processpool.h"
 
class cgi_conn {
public:
    cgi_conn() : m_sockfd(-1), m_read_idx(-1)
    {
        memset(m_buf, 0, sizeof(m_buf));
    }
 
public:
    void init(int epollfd, int sockfd, const sockaddr_in& client_addr)
    {
        m_epollfd = epollfd;
        m_sockfd = sockfd;
        m_address = client_addr;
        m_read_idx = 0;
    }
 
    void process()
    {
        int idx = 0;
        int ret = 1;
 
        //循环读取和分析客户数据
        while(true) {
            idx = m_read_idx;
            ret = recv(m_sockfd, m_buf+idx, BUFFER_SIZE-1-idx, 0);
            printf("recv ret=%d\n", ret);
 
            //如果读操作发生错误，则关闭客户链接，如果只是暂时无数据可读，则退出循环
            if(ret < 0) {
                if(errno != EAGAIN) {
                    removefd(m_epollfd, m_sockfd);
                }
                break;
            }
            //如果对方关闭，本服务器也关闭
            else if(ret == 0) {
                removefd(m_epollfd, m_sockfd);
                break;
            }
            else {
                m_read_idx += ret;
                printf("user content is: %s", m_buf);
                //如果遇到字符CRLF,则开始处理客户请求
                for(; idx<m_read_idx; ++idx) {
                    if((idx >= 1) && (m_buf[idx-1] == '\r') && (m_buf[idx] == '\n')) //这里查找CRLF采用简单遍历已读数据的方法
                        break;
                }
            }
            //如果没有遇到字符CRLF,则需要读取更多客户数据
            if(idx == m_read_idx) {
                continue;
            }
            m_buf[idx-1] = '\0';
 
            char* file_name = m_buf;
            printf("file_name=%s\n", file_name);
 
            //判断客户要运行的CGI程序是否存在
            if(access(file_name, F_OK) == -1) {
                removefd(m_epollfd, m_sockfd);   //不存在就不连接了
                printf("file not found\n");
                break;
            }
 
            //创建子进程来执行CGI程序
            ret = fork();
            if(ret == -1) {
                removefd(m_epollfd, m_sockfd);
                break;
            }
            else if(ret > 0) {
                //父进程只需关闭连接
                removefd(m_epollfd, m_sockfd);
                break;   //父进程break
            }
            else {
                //子进程将标准输出定向到sockfd_,并执行CGI程序
                close(STDOUT_FILENO);
                dup(m_sockfd);
                execl(m_buf, m_buf, 0);
                exit(0);
            }
        }
    }
 
private:
    //读缓冲区的大小
    static const int BUFFER_SIZE = 1024;
    static int  m_epollfd;
    int         m_sockfd;
    sockaddr_in m_address;
    char        m_buf[BUFFER_SIZE];
    //标记缓冲区中已读入客户数据的最后一个字节的下一个位置
    int         m_read_idx;
};
 
int cgi_conn::m_epollfd = -1;
int main(int argc, char** argv)
{
    if (argc <= 2) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        //return -1;
    }
    const char* ip = "127.0.0.1";//argv[1];
    int port = 8011;//atoi(argv[2]);
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);
 
    int ret = 0;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);//ok
    //inet_pton(AF_INET, ip, &servaddr.sin_addr);//使用"127.0.0.1"，实体机windows客户端连接不进来虚拟机
    address.sin_port = htons(port);
 
    int on = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
 
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    if(ret == -1) {
        printf("what: %m\n");
        return -1;
    }
 
    ret = listen(listenfd, 5);
    assert(ret != -1);
 
    processpool<cgi_conn>* pool = processpool<cgi_conn>::create(listenfd);
    if(pool) {
        pool->run();
        delete pool;
    }
 
    close(listenfd);
 
    return 0;
}
