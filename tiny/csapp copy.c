#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "csapp.h"

struct sockaddr_in {
    uint16_t            sin_family;
    uint16_t            sin_port;
    struct in_addr      sin_addr;
    unsinged char       sin_zero[8];
};

struct sockadder {
    unit16_t        sa_family;
    char            sa_data[14];
}

int socket(int domain, int type, int protocol);
int connect(int clientfd, const struct sockaddr *addr, socklen_t addrlen);
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int listen(int sockfd, int backlog);
int accept(int listenfd, struct sockaddr *addr, int *addrlen);
int getaddrinfo(const char *host, const char*service, const struct addrinfo *hints, struct addrinfo **result);
void freeaddrinfo(struct addrinfo *result);
const char *gai_strerror(int errcode);
int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, size_t hostlen, char *service, size_t servlen, int flags);
int open_clientfd(char *hostname, char *port);
int open_listenfd(char *port);
void echo(int connfd);

struct addrinfo {
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    char            *ai_canonname;
    size_t           ai_addrlen;
    struct sockaddr *ai_addr;
    struct addrinfo *ai_next;
}

int main(int argc, char **argv) {
    struct addrinfo *p, *listp, hints;
    char buf[MAXLINE];
    int rc, flags;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <domain name>\n", argv[0]);
        exit(0);
    }

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INEF;
    hints.ai_socktype = SOCK_STREAM;
    if ((rc = getaddrinfo(argv[1], NULL, &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rc));
        exit(1);
    }

    flags = NI_NUMERICHOST;
    for (p = listp; p; p = p->ai_next) {
        Getnameinfo(p->ai_addr, p->ai_addrlen, buf, MAXLINE, NULL, 0, flags);
        printf("%s\n", buf);
    }

    Freeaddrinfo(listp);
    
    exit(0);
}

int open_clientfd(char *hostname, char *port) {
    int clientfd;
    struct addrinfo hints, *listp, *p;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;
    hints.ai_flags |= AI_ADDRCONFIG;
    Getaddrinfo(hostname, port, &hints, &listp);

    for (p = listp; p; p = p->ai_next) {
        if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
            continue;

        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
            break;
        Close(clientfd);
    }
    Freeaddrinfo(listp);
    if (!p)
        return -1;
    else
        return clientfd;
}

int open_listenfd(char *port) {
    struct addrinfo hints, *listp, *p;
    int listenfd, optval=1;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    hints.ai_flags |= AI_NUMERICSERV;
    Getaddrinfo(NULL, port, &hints, &listp);

    for (p = listp; p; p = p->ai_next) {
        if ((listenfd = socket(p->ai_family, p->ai_socktype, p->protocol)) < 0)
            continue;

        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
            break;
        Close(listenfd); 
    }

    Freeaddrinfo(listp);
    if (!p)
        return -1;
    
    if (listen(listenfd, LISTENQ) < 0) {
        Close(listenfd);
        return -1;
    }
    return listenfd;
}

// echo 클라이언트를 위한 코드를 보여준다. 서버와의 연결을 수립한 이후에 클라이언트는 표준 입력에서 텍스트 줄을 반복해서 읽는 루프에 진입하고, 서버에 텍스트 줄을 전송하고, 서버에서 echo 줄을 읽어서 그 결과를 표준 출력으로 인쇄한다. 루프는 fgets가 EOf 표준 입력을 만나면 종료하는데, 이유는 사용자가 컨트롤 + d를 눌렀거나 파일로 텍스트 줄을 모두 소진했기 때문이다. 루프가 종료한 후에 클라이언트는 식별자를 닫는다.
int main(int argc, char **argv)
{
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }
    host = argv[1];
    port = argv[2];

    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&rio, clientfd);

    while (Fgets(buf, MAXLINE, stdin) != NULL) {
        Rio_writen(clientfd, buf, strlen(buf));
        rio_readlineb(&rio, buf, MAXLINE);
        Fputs(buf, stdout);
    }
    Close(clientfd);
    exit(0);
}

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; // clientaddr 변수는 accept로 보내지는 소켓 주소 구조체다.  accept가 리턴하기 전에 clientaddr에는 연결의 다른 쪽 끝의 클라이언트의 소켓 주소로 채워진다. 어떻게 clientaddr을 struct sockaddr_in이 아닌 struct sockaddr_storage형으로 선언하였는지 유의하라. sockaddr_storage구조체는 모든 형태의 소켓 주소를 저장하기에 충분히 크며, 이것은 코드를 프로토콜 독립적으로 유지해준다.
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        echo(connfd);
        Close(connfd);
    }
    exit(0);
}

// echo 루틴을 위한 코드를 보여주며, 
void echo(int connfd) {
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) { // rio_readlineb함수가 EOR를 만날 때까지 텍스트 줄을 반복해서 읽고 써준다.
        printf("server received %d bytes\n", (int)n);
        Rio_writen(connfd, buf, n);
    }
}