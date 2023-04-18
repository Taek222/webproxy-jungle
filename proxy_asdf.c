#include "csapp.h"

void proxy(int connfd);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
int parse_uri(char *uri, char *request_ip, char *port, char *filename);
int make_request(rio_t *client_rio, char *hostname, char *path, int port, char *hdr, char *method);

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *request_hdr_format = "%s %s HTTP/1.0\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *Accept_hdr = "    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *EOL = "\r\n";

/*
파일 디스크립터: 컴퓨터 프로그램이 파일 또는 기타 입/출력 리소스를 참조하는 방법
책에 대한 도서관의 참조 번호와 같음. 리소스는 일반적으로 운영 체제에서 관리함.
*/

/*
rio_t 구조체는 Robust I/O(Rio)라이브러리에서 사용되는 데이터 구조체

typedef struct {
    int rio_fd;                 // 파일 디스크립터
    ssize_t rio_cnt;            // 남아 있는 데이터의 바이트 수
    char *rio_bufptr;           // 다음에 읽어들일 데이터의 위치
    char rio_buf[RIO_BUFSIZE];  // 내부 버퍼
} rio_t;

*/

int main(int argc, char **argv)
{
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  char client_hostname[MAXLINE], client_port[MAXLINE]; /* 클라이언트의 IP, PORT */

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(struct sockaddr_storage);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); /* 소켓 디스크립터(connfd, 프록시 서버와 클라이언트 간의 통신에 사용하는 연결 식별자) */
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    printf("Connected to (%s, %s)\n", client_hostname, client_port);
    proxy(connfd);
    Close(connfd); /* 클라이언트와의 연결 종료 */
  }
  return 0;
}

void proxy(int client_fd) // 나의 doit
{
  char hostname[MAXLINE], path[MAXLINE]; /* 프록시가 요청을 보낼 서버의 IP, 파일 경로 */
  int port;

  char buf[MAXLINE], hdr[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];

  int server_fd;

  rio_t client_rio; /* 클라이언트와 */
  rio_t server_rio; /* 서버와 */

  Rio_readinitb(&client_rio, client_fd);         /* 클라이언트와의 connectoin */
  Rio_readlineb(&client_rio, buf, MAXLINE);      /* 클라이언트의 요청 읽기 */
  sscanf(buf, "%s %s %s", method, uri, version); /* 클라이언트의 요청 파싱 => method, uri, version */

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  {
    /* 클라이언트의 요청이 GET이나 HEAD가 아니면 501 에러 */
    printf("[PROXY] 501 ERROR\n");
    clienterror(client_fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }

  parse_uri(uri, hostname, path, &port); /* uri 파싱 => hostname, path, port에 할당 */

  /* end server로 보낼 요청 메시지 헤더를 생성 */
  if (!make_request(&client_rio, hostname, path, port, hdr, method))
  {
    clienterror(client_fd, method, "501", "request header error",
                "Request header is wrong");
  }
  /* end server에 요청 보낼 준비 완료! */
  /* hostname, port 서버에 대한 connection 열기 => 서버와의 소켓 디스크립터 생성 */
  server_fd = Open_clientfd(hostname, port);
  /*
  Rio_readinitb: rio_t 구조체를 초기화하는 함수
  serve_fd라는 파일 디스크립터를 사용하여 rio_t 구조체인 server_rio 초기화
  */
  Rio_readinitb(&server_rio, server_fd); /* 서버 소켓과 연결 */
  printf("2.[I'm proxy] proxy -> server\n");
  /*
  Rio_writen: 소켓을 통해 데이터를 씀
  소켓 파일 디스크립터(데이터를 보낼 데상) / HTTP 요청 메시지를 가리키는 포인터(보낼 데이터)) / HTTP 요청 메시지의 길이(보낼 데이터의 크기)
  */
  Rio_writen(server_fd, hdr, strlen(hdr)); /* 서버에 HTTP request 메시지를 보냄 */

  /* end server에 요청 완료! */
  size_t n;
  /* 서버에서 받은 응답 읽기 */
  /*
  Rio_readlineb: 소켓을 통해 데이터를 읽는 함수
  rio_t 구조체 변수 / 응답 메시지를 저장하기 위한 버퍼 / 버퍼의 크기
  */
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
  {
    printf("4. [I'm proxy] server -> proxy\n");

    printf("5. [I'm proxy] proxy -> client\n");
    Rio_writen(client_fd, buf, n); /* 서버에서 받은 응답 메시지를 클라이언트에게 전송 */
  }
  Close(server_fd); /* 서버와의 연결 종료 */
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* $end clienterror */
int parse_uri(char *uri, char *uri_ptos, char *host, char *port)
{
  char *ptr;

  if (!(ptr = strstr(uri, "://")))
    return -1;
  ptr += 3;
  strcpy(host, ptr); // host = www.google.com:80/index.html

  if ((ptr = strchr(host, ':')))
  {              // strchr(): 문자 하나만 찾는 함수 (''작은따옴표사용)
    *ptr = '\0'; // host = www.google.com
    ptr += 1;
    strcpy(port, ptr); // port = 80/index.html
  }
  else
  {
    if ((ptr = strchr(host, '/')))
    {
      *ptr = '\0';
      ptr += 1;
    }
    strcpy(port, "80");
  }

  if ((ptr = strchr(port, '/')))
  {              // port = 80/index.html
    *ptr = '\0'; // port = 80
    ptr += 1;
    strcpy(uri_ptos, "/"); // uri_ptos = /
    strcat(uri_ptos, ptr); // uri_ptos = /index.html
  }
  else
    strcpy(uri_ptos, "/");

  return 0; // function int return => for valid check
}

int make_request(rio_t *client_rio, char *hostname, char *path, int port, char *hdr, char *method)
{

  char req_hdr[MAXLINE], additional_hdf[MAXLINE], host_hdr[MAXLINE];
  char buf[MAXLINE];
  char *HOST = "Host";
  char *CONN = "Connection";
  char *UA = "User-Agent";
  char *P_CONN = "Proxy-Connection";
  sprintf(req_hdr, request_hdr_format, method, path);

  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
  {
    if (!strcmp(buf, EOL))
      break;

    if (!strncasecmp(buf, HOST, strlen(HOST)))
    {

      strcpy(host_hdr, buf);
      continue;
    }

    if (strncasecmp(buf, CONN, strlen(CONN)) && strncasecmp(buf, UA, strlen(UA)) && strncasecmp(buf, P_CONN, strlen(P_CONN)))
    {

      strcat(additional_hdf, buf);
    }
  }

  if (!strlen(host_hdr))
  {
    sprintf(host_hdr, host_hdr_format, hostname);
  }

  sprintf(hdr, "%s%s%s%s%s%s%s",
          req_hdr,
          host_hdr,
          user_agent_hdr,
          Accept_hdr,
          connection_hdr,
          proxy_connection_hdr,
          EOL);
  if (strlen(hdr))
    return 1;
  return 0;
}