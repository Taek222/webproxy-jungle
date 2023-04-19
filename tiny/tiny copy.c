/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

// Tiny는 반복실행 서버로 명령줄에서 넘겨받은 포트로의 연결 요청을 듣는다.
int main(int argc, char **argv)
{ // 넘어오는 인자의 개수와 인자 배열 포인팅
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  { // 인자 개수가 2개 인지 체크(포트 번호가 입력됐어?)
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // open_listenfd 함수를 호출해서 듣기 소켓을 오픈한다.
  while (1)
  { // Tiny는 전형적인 무한 서버 루프를 실행하고,
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 반복적으로 연결 요청을 접수하고
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // 트랜잭션을 수행하고
    Close(connfd); // 자신 쪽의 연결 끝을 닫는다.
  }
}

// 한 개의 HTTP 트랜잭션을 처리한다.
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  // 먼저, 요청 라인을 읽고 분석한다.
  // 그림 10.8의 rio_readlineb함수를 사용해서 요청 라인을 읽어들인다는 점에 유의
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // 요청 헤더를 3가지로 나눠서 입력 받는다?

  // Tiny는 GET 메소드만 지원한다. 만일 클라이언트가 다른 메소드(POST 같은)를 요청하면, 에러 메시지를 보내고, main 루틴으로 돌아오고, 그 후에 연결을 닫고 다음 연결 요청을 기다린다.
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); // 그렇지 않으면 읽어들이고, 다른 요청 헤더들을 무시한다.

  // Parse URI from GET request
  // URI를 파일 이름과 비어 있을 수도 있는 CGI 인자 스트링으로 분석하고, 요청이 정적 또는 동적 컨텐츠를 위한 것인지 나타내는 플래그를 설정한다. 만일 이 파일이 디스크 상에 있지 않으면, 에러 메시지를 즉시 클라이언트에 보내고 리턴한다.
  is_static = parse_uri(uri, filename, cgiargs);
  if (stat(filename, &sbuf) < 0)
  {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't read the file");
    return;
  }

  // 만일 요청이 정적 컨텐츠를 위한 것이라면,
  if (is_static)
  { // Serve static content
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    { // 우리는 이 파일이 보통 파일이라는 것과 읽기 권한을 가지고 있는지를 검증한다.
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method); // 만일 그렇다면(파일이 보통 파일이라는 것과 읽기 권한을 가지고 있다면), 정적 컨텐츠를 클라이언트에게 제공한다.
  }
  else
  { // 만일 요청이 동적 컨텐츠에 대한 것이라면
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    { // 이 파일이 실행 가능한지 검증하고,
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method); // 만일 실행가능하다면 진행해서 동적 컨텐츠를 제공한다.
  }
}

// Tiny clienterror 에러 메시지를 클라이언트에게 보낸다.
// Tiny는 실제 서버에서 볼 수 있는 많은 에러 처리 기능들이 빠져 있다. 그러나 일부 명백한 오류에 대해서는 체크하고 있으며, 이들을 클라이언트에게 보고한다. clienterror함수는 HTTP 응답을 응답 라인에 적절한 상태 코드와 상태 메시지와 함께 클라이언트에 보내며, 브라우저 사용자에게 에러를 설명하는 응답 본체에 HTML 파일도 함께 보낸다. HTML 응답은 본체에서 컨텐츠의 크기와 타입을 나타내야 한다는 점을 기억하라. 그래서 HTML 컨텐츠를 한 개의 스트링으로 만드는 선택을 하였으며, 이로 인해 그 크기를 쉽게 결정할 수 있다. 또한, 우리가 견고한 rio_writen 함수를 모든 출력에 대해서 사용하고 있다는 점에 주목해야 한다.
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // Print the HTTP response
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content_type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// Tiny read_requesthdrs 요청 헤더를 읽고 무시한다.
// Tiny는 요청 헤더 내의 어떤 정보도 사용하지 않는다. 단순히 read_requesthrs 함수를 호출해서 이들을 읽고 무시한다.
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
  { // 요청 헤더를 종료하는 빈 텍스트 줄이 while문 절에서 체크하고 있는 carriage return과 line feed 쌍으로 구성되어 있다는 점에 주목하라. (뭔솔이여...)
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

// Tiny parse_uri HTTP URI를 분석한다.
// Tiny는 정적 컨텐츠를 위한 홈 디렉토리가 자신의 현재 디렉토리이고, 실행파일의 홈 디렉토리는 /cgi-bin이라고 가정한다. 스트링 cgi-bi을 포함하는 모든 URI는 동적 컨텐츠를 요청하는 것을 나타낸다고 가정한다. 기본 파일 이름은 ./home.html이다.
// parse_uri함수는 이 정책들을 구현하고 있다. 이것은 URI를 파일 이름과 옵션으로 GCI 인자 스트링을 분석한다.
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin"))
  {                                  // 만일 요청이 정적 컨텐츠를 위한 것이라면
    strcpy(cgiargs, "");             // CGI 인자 스트링을 지우고
    strcpy(filename, ".");           // URI를 ./index.html 같은 상대 리눅스 경로이름으로 변환한다.
    strcat(filename, uri);           // URI를 ./index.html 같은 상대 리눅스 경로이름으로 변환한다.
    if (uri[strlen(uri) - 1] == '/') // 만일 URI가 '/' 문자로 끝난다면
      strcat(filename, "home.html"); // 기본 파일 이름을 추가한다.
    return 1;
  }
  else
  {                        // 반면에 만일 이 요청이 동적 컨텐츠를 위한 것이라면
    ptr = index(uri, '?'); // 모든 인자들을 추출하고 (else구문까지)
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, "."); // 나머지 URI 부분을 상대 리눅스 파일이름으로 변환한다.
    strcat(filename, uri); // 나머지 URI 부분을 상대 리눅스 파일이름으로 변환한다.
    return 0;
  }
}

// Tiny serve_static은 정적 컨텐츠를 클라이언트에게 서비스한다.
// Tiny는 다섯 개의 서로 다른 정적 컨텐츠 타입을 지원한다. HTML 파일, 무형식 텍스트 파일, GIF, PNG, JPEG으로 인코딩된 영상.
// serve_static함수는 지역 파일의 내용을 포함하고 있는 본체를 갖는 HTTP 응답을 보낸다.
void serve_static(int fd, char *filename, int filesize, char *method)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // Send response headers to client
  get_filetype(filename, filetype);    // 먼저, 파일 이름의 접미어 부분을 검사해서 파일 타입을 결정하고
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 클라이언트에 응답 줄과 응답 헤더를 보낸다. 빈 줄 한 개가 헤더를 종료하고 있다는 점에 주목해야 한다. (Rio_writen 코드 까지)
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf)); // 다음으로, 요청한 파일의 내용을 연결 식별자 fd로 복사해서 응답 본체를 보낸다.
  printf("Response headers:\n");
  printf("%s", buf);

  if (!strcasecmp(method, "HEAD"))
  {
    return;
  }
  // Send response body to client
  srcfd = Open(filename, O_RDONLY, 0); // 읽기 위해서 filename을 오픈하고 식별자를 얻어온다.
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // 리눅스 mmap함수는 요청한 파일을 가상메모리 영역으로 매핑한다. 9.8절에서 mmap에 대한 우리의 논의를 기억.... mmap을 호출하면 srcfd의 첫 번째 filesize 바이트를 주소 srcp에서 시작하는 사적 읽기-허용 가상메모리 영역으로 매핑한다.
  srcp = malloc(filesize);
  Rio_readn(srcfd, srcp, filesize);
  Close(srcfd);                   // 파일을 메모리로 매핑한 후에 더 이상 이 식별자는 필요없으며, 그래서 이 파일을 닫는다. 이렇게 하지 않으면 치명적일 수 있느 ㄴ메모리 누수가 발생할 수 있다.
  Rio_writen(fd, srcp, filesize); // 실제로 파일을 클라이언트에게 전송한다. rio_writen함수는 주소 srcp에서 시작하는 filesize 바이트(물론, 이것은 요청한 파일에 매핑되어 있다)를 클라이언트의 연결 식별자로 복사한다.
  // Munmap(srcp, filesize); // 매핑된 가상메모리 주소를 반환한다. 이것은 치명적일 수 있는 메모리 누수를 피하는 데 중요하다.
  free(srcp);
}

// get_filetype - Derive file type from filename
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

// Tiny는 자식 프로세스를 fork하고, 그 후에 CGI 프로그램을 자식의 컨텍스트에서 실행하며 모든 종류의 동적 컨텐츠를 제공한다.
// serve_dynamic함수는 클라이언트에 성공을 알려주는 응답 라인을 보내는 것으로 시작된다. CGI 프로그램은 응답의 나머지 부분을 보내야 한다. 이것은 우리가 기대하는 것만큼 견고하지 않은데, 그 이유는 이것이 CGI 프로그램이 에러를 만날 수 있다는 가능성을 염두에 두지 않았기 때문이라는 점을 주목해야 한다.
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  // Return first part of HTTP response
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (!strcasecmp(method, "HEAD"))
    return;

  if (Fork() == 0)
  { // 이 응답의 첫 번째 부분을 보낸 후에, 새로운 자식 프로세스를 fork한다.
    // Real server would set all CGI vars here
    setenv("QUERY_STRING", cgiargs, 1);   // 자식은 QUERY_STRING 환경변수를 요청 URI의 CGI 인자들로 초기화한다. 실제 서버는 여기서 다른 CGI 환경변수들도 마찬가지로 설정한다는 점에 유의하라. (단순성을 위해 이 단계 생략)
    Dup2(fd, STDOUT_FILENO);              // 자식은 자식의 표준 출력을 연결 파일 식별자로 재지정하고,
    Execve(filename, emptylist, environ); // 그후에 CGI 프로그램을 로드하고 실행한다. CGI 프로그램이 자식 컨텍스트에서 실행되기 때문에 execve함수를 호출하기 전에 존재하던 열린 파일들과 환경변수들에도 동일하게 접근할 수 있다. 그래서 CGI 프로그램이 표준 출력에 쓴느 모든 것은 직접 클라이언트 프로세스로 부모 프로세스의 어떤 간섭도 없이 전달된다.
  }
  Wait(NULL); // 한편, 부모는 자식이 종료되어 정리되는 것을 기다리기 위해 wait함수에서 블록된다.
}