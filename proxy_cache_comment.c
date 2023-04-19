/*
    Web Proxy
    : Web browser와 End server 사이에서 중개자 역할을 하는 프로그램
    : 브라우저는 프록시에 연결, 프록시가 서버로 대신 연결하여 요청 전달
    : 서버가 프록시에 응답, 프록시가 브라우저로 응답 전달

    1) 방화벽: 브라우저가 프록시를 통해서만 방화벽 너머의 서버에 연결할 수 있음
    2) 익명화: 프록시는 브라우저의 모든 식별 정보를 제거하여 전달
    3) 캐시: 프록시는 서버 개체의 복사본을 저장, 다시 통신하지 않고 캐시에서 읽어 향후 요청에 응답

    Part 1
    : 들어온 연결 수락
    : 요청 분석 및 웹 서버로 전달
    : 응답 분석 및 클라이언트로 전달

    Part 2
    : 여러 동시 연결을 처리하도록

    Part 3
    : 프록시에 캐싱 추가
    : 최근에 액세스한 웹 콘텐츠의 간단한 메인 메모리 캐시 사용
*/

#include "csapp.h"

// 최대 캐시 사이즈 1 MiB, 메타 데이1터 등 불필요한 바이트는 무시
#define MAX_CACHE_SIZE 1049000

// 최대 객체 사이즈 100 KiB
#define MAX_OBJECT_SIZE 102400

// 최근 사용 횟수가 가장 적은 페이지를 삭제하는 방법인 LRU를 사용 (Least Recently Used)
#define LRU_PRIORITY 9999
#define MAX_OBJECT_SIZE_IN_CACHE 10

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void *thread(void *vargsp);
void doit(int connfd);
int check_cache(char *url, char *uri, int connfd);
void make_header(char *final_header, char *hostname, char *path, rio_t *client_rio);
int server_connection(char *hostname, int port);
void parse_uri(char *uri, char *hostname, int *port, char *path);

void cache_init();
int cache_find(char *url);
void cache_uri(char *uri, char *buf);

void read_before(int i);
void read_after(int i);

/*
    [Synchronization]
    : 캐시에 대한 접근은 thread-safe 해야 함
    : Pthreads readers-writers locks, Readers-Writers with semaphores 옵션 등이 있음
*/

typedef struct
{
  char cache_object[MAX_OBJECT_SIZE];
  char cache_url[MAXLINE];

  // 우선순위가 높을 수록 오래된 페이지, 즉 수가 낮을 수록 삭제될 가능성이 높음
  int priority;
  int is_empty;

  int read_count;
  sem_t wmutex;
  sem_t rdcntmutex;

} cache_block;

typedef struct
{
  cache_block cache_items[MAX_OBJECT_SIZE_IN_CACHE];
} Cache;

Cache cache;

int main(int argc, char **argv)
{
  // 여러 개의 동시 연결을 처리하기 위해 스레드 사용
  pthread_t tid;

  cache_init();

  // listenfd와 connfd를 구분하는 이유
  // multi client가 요청할 때를 대비, 대기타는 스레드 따로 연결하는 스레드 따로 있어야 함
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  // 파라미터 개수가 항상 2개여야 하는 이유
  // 프로그램 자체가 첫번째 파라미터, 필요한 파라미터는 1개뿐이기 때문에 argc는 2여야 함
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /*
      한 Client가 종료되었을 때, 남은 다른 Client에 간섭가지 않도록 하기 위함

      더 자세히
      한 Client가 비정상적인 종료를 한 경우, Server는 이를 모르고 해당 Client로 Response를 보낼 수도 있음
      이때 잘못됐다는 Signal이 날라옴, Signal을 받으면 전체 프로세스 종료
      이 경우 연결되어 있는 다른 Client들도 종료될 수 있기 때문에 이 시그널을 무시하라는 의미
  */
  Signal(SIGPIPE, SIG_IGN);

  // 소켓의 연결을 위해 Listen 소켓 Open
  listenfd = Open_listenfd(argv[1]);

  while (1)
  {
    clientlen = sizeof(clientaddr);

    // 반복적으로 Connect 요청
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    /*
        tid: 스레드 식별자, 스레드마다 식별 번호를 지정해주어야 함
        NULL: 스레드 option 지정, 기본이 NULL
        thread: 스레드가 해야 하는 일을 함수로 만들어, 그 함수명을 지정
        (void *)connfd: 스레드 함수의 매개변수를 지정, 즉 thread((void *)connfd)로 호출한 셈
    */
    Pthread_create(&tid, NULL, thread, (void *)connfd);
  }
}

/*
    멀티 스레드: 여러 개의 동시 요청 처리

    동시 서버를 구현하는 가장 간단한 방법은 새 스레드를 생성하는 것
    각 스레드가 새 연결 요청을 처리함

    1) 메모리 누수를 방지하려면 스레드를 분리 모드로 실행
    2) getaddrinfo 함수를 사용하면 thread safe함
*/
void *thread(void *vargsp)
{
  int connfd = (int)vargsp;

  // 메인 스레드로부터 현재 스레드를 분리시킴
  // 분리시키는 이유: 해당 스레드가 종료되는 즉시 모든 자원을 반납할 것(free)을 보증, 분리하지 않으면 따로 pthread_join(pid)를 호출해야함
  Pthread_detach(Pthread_self());

  // 트랜잭션 수행 후 Connect 소켓 Close
  doit(connfd);
  Close(connfd);
}

// doit: 한 개의 트랜잭션을 수행하는 함수
void doit(int connfd)
{
  int serverfd, port;
  char server_header[MAXLINE];
  char buf[MAXLINE], cachebuf[MAX_OBJECT_SIZE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], url[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE];
  rio_t clientrio, serverrio;

  // Rio_readinitb(): clientrio와 connfd를 연결
  Rio_readinitb(&clientrio, connfd);
  Rio_readlineb(&clientrio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET"))
  {
    printf("Proxy does not implement the method.");
    return;
  }

  // check_cache 함수를 이용해서 캐시에 등록되어 있는지 확인, 있으면 url에 저장함
  if (!(check_cache(url, uri, connfd)))
  {
    return;
  }

  // URI를 파싱하여 hostname, port, path를 얻고, 조건에 부합하는 헤더 생성
  parse_uri(uri, hostname, &port, path);
  make_header(server_header, hostname, path, &clientrio);

  // 서버와의 연결 (인라인 함수)
  serverfd = server_connection(hostname, port);

  // Rio_readinitb(): serverrio와 serverfd를 연결
  Rio_readinitb(&serverrio, serverfd);
  Rio_writen(serverfd, server_header, strlen(server_header));

  // 서버로부터 응답을 받고 클라이언트로 보내줌
  size_t response;
  int bufsize = 0;
  while ((response = Rio_readlineb(&serverrio, buf, MAXLINE)) != 0)
  {
    bufsize += response;

    // 최대 개체 사이즈보다 작으면, 받은 응답을 캐시에 저장
    if (bufsize < MAX_OBJECT_SIZE)
    {
      strcat(cachebuf, buf);
    }

    Rio_writen(connfd, buf, response);
  }

  Close(serverfd);

  if (bufsize < MAX_OBJECT_SIZE)
  {
    // URL에 cachebuf 저장
    cache_uri(url, cachebuf);
  }
}

int check_cache(char *url, char *uri, int connfd) // 문자 배열에 대한 포인터(url), 문자 배열에 대한 포인터( uri) 및 정수(connfd). 정수를 반환한다.
{
  strcpy(url, uri); // uri의 내용을 url에 복사한다. 이는 cache_find 함수가 URI가 아닌 URL을 예상하기 때문에 수행된다.
  int index;        // 캐시 항목이 발견되면 인덱스를 저장하는 데 사용할 index라는 정수 변수를 선언합니다.

  if ((index = cache_find(url)) != -1) // 캐시에 URL이 존재하는지 확인하기 위해 cache_find 함수를 호출한다. 그렇다면 캐시 항목의 인덱스는 index 변수에 저장된다.
  {
    read_before(index); // 클라이언트에 제공되는 동안 다른 스레드가 캐시 항목을 수정하지 못하도록 지정된 인덱스에서 캐시 항목에 대한 읽기 잠금을 획득한다.

    Rio_writen(connfd, cache.cache_items[index].cache_object, strlen(cache.cache_items[index].cache_object)); // Rio_write 기능을 사용하여 캐시된 개체를 클라이언트 연결에 쓴다. 개체는 캐시 항목의 'cache_object' 필드에 저장된다.

    read_after(index); // 캐시 항목에 대한 읽기 잠금을 해제한다.

    return 0; // 개체가 캐시에서 제공되었음을 나타내기 위해 0을 반환한다.
  }

  return 1; //  개체가 캐시에서 발견되지 않았음을 나타내기 위해 1을 반환한다. 이 줄은 올바른 구문을 위해 if 문 외부로 이동해야 한다.
}
void make_header(char *final_header, char *hostname, char *path, rio_t *client_rio)
{
  char buf[MAXLINE]; // 요청 헤더를 읽어올 버퍼
  char request_header[MAXLINE], host_header[MAXLINE], etc_header[MAXLINE]; // 각각 GET 요청 헤더, 호스트 헤더, 기타 헤더를 저장할 버퍼?

  sprintf(request_header, "GET %s HTTP/1.0\r\n", path); // GET 요청 헤더를 생성한다. 요청할 경로는 path로부터 받아온다.

  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) // 클라이언트로부터 요청 헤더를 한 줄씩 읽어온다.
  {
    if (strcmp(buf, "\r\n") == 0) // 읽어온 헤더가 끝에 도달한 경우, 반복문을 종료한다.
    {
      break;
    }

    if (!strncasecmp(buf, "Host", strlen("Host"))) // 읽어온 헤더가 호스트 헤더인 경우, host_header 버퍼에 저장한다.
    {
      strcpy(host_header, buf);
      continue;
    }

    if (strncasecmp(buf, "User-Agent", strlen("User-Agent")) && strncasecmp(buf, "Connection", strlen("Connection")) && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection"))) // 읽어온 헤더가 User-Agent, Connection, Proxy-Connection 헤더가 아닌 경우, etc_header 버퍼에 저장한다.
    {
      // 위 세가지 헤더 이외의 다른 헤더가 요청되었을 때, 따로 저장하여 전달
      strcat(etc_header, buf);
    }
  }

  if (strlen(host_header) == 0)
  {
    sprintf(host_header, "Host: %s\r\n", hostname); // 호스트 헤더를 생성한다. 호스트 이름은 hostname으로부터 받아온다.
  }

  sprintf(final_header, "%s%s%s%s%s%s%s", // 최종 헤더를 생성한다. request_header, host_header, Connection, Proxy-Connection, User-Agent 헤더와 etc_header를 모두 포함한다.
          request_header,
          host_header,
          "Connection: close\r\n",
          "Proxy-Connection: close\r\n",
          user_agent_hdr,
          etc_header,
          "\r\n");
}

// 이것은 문자열 hostname과 정수 port라는 두 개의 매개변수를 취하는 server_connection이라는 함수입니다. 서버에 대한 소켓 연결에 대한 파일 설명자를 나타내는 정수를 반환합니다.
inline int server_connection(char *hostname, int port)
{
  char portStr[100]; // port 정수의 문자열 표현을 저장하기 위해 크기 100의 문자 배열 portStr을 선언한다.

  // Open_clientfd 함수는 port를 문자 파라미터로 넣어야 함
  sprintf(portStr, "%d", port); // 정수 port를 문자열로 변환하고 portStr에 저장한다. sprintf() 함수는 portStr 배열에 일련의 문자 형식을 지정하고 쓴다.

  return Open_clientfd(hostname, portStr); // 서버에 열린 소켓 연결의 파일 디스크립터를 반환한다. Open_clientfd() 함수를 호출하고 hostname 및 portStr을 매개변수로 전달한다. Open_clientfd() 함수는 hostname 및 portStr 매개변수로 지정된 서버에 연결된 클라이언트 소켓에 대한 파일 디스크립터를 생성하고 반환한다.
}

// 이 함수는 URI(Uniform Resource Identifier)를 구성 부분인 호스트 이름, 포트 및 경로로 구문 분석한다.
void parse_uri(char *uri, char *hostname, int *port, char *path) // 이 함수는 호스트 이름, 포트 및 경로를 보유할 변수에 대한 포인터뿐만 아니라 입력으로 URI를 사용한다.
{
  char *first = strstr(uri, "//"); // 이 함수는 URI에서 하위 문자열 "//"을 검색한다.
  first = first != NULL ? first + 2 : uri; // 발견되면 'first' 포인터가 바로 뒤에 오는 문자(즉, 호스트 이름의 시작 부분)로 설정된다. 찾을 수 없으면 'first'가 URI의 시작 부분으로 설정된다.

  char *next = strstr(first, ":"); // 이 함수는 first에서 하위 문자열 ":"을 검색한다. 발견되면 'next'는 바로 뒤에 오는 문자(즉, 포트의 시작 부분)로 설정된다. 찾을 수 없으면 next를 사용하여 경로의 시작 부분을 검색합니다.

  *port = 80; // URI에 포트가 지정되지 않은 경우 포트는 기본 HTTP 포트인 80으로 설정된다.
  if (next)   // URI에 포트가 지정된 경우 함수는 URI에서 호스트 이름, 포트 및 경로를 구문 분석합니다.
  {
    *next = '\0'; // next가 가리키는 문자는 호스트 이름 문자열을 종료하기 위해 '\0'으로 설정된다.
    sscanf(first, "%s", hostname); // 이 함수는 호스트 이름 문자열을 hostname 변수로 읽는다.
    sscanf(next + 1, "%d%s", port, path); // 이 함수는 포트 번호(정수)와 경로 문자열을 각각 port 및 path 변수로 읽는다.
  }
  else // URI에 지정된 포트가 없으면 함수는 URI에서 호스트 이름과 경로를 구문 분석한다.
  {
    next = strstr(first, "/"); // 이 함수는 경로의 시작 부분을 검색한다.

    if (next) // 경로가 발견되면 함수는 URI에서 호스트 이름과 경로를 구문 분석한다.
    {
      *next = '\0'; // next가 가리키는 문자는 호스트 이름 문자열을 종료하기 위해 '\0'으로 설정된다.
      sscanf(first, "%s", hostname); // 이 함수는 호스트 이름 문자열을 hostname 변수로 읽는다.

      *next = '/'; // next가 가리키는 문자는 경로 문자열을 종료하기 위해 다시 '/'로 설정된다.
      sscanf(next, "%s", path); // 이 함수는 경로 문자열을 path 변수로 읽는다.
    }
    else // 경로가 없으면 함수는 호스트 이름만 구문 분석한다.
    {
      sscanf(first, "%s", hostname); // 이 함수는 호스트 이름 문자열을 hostname 변수로 읽는다다.
    }
  }
}

// 이 코드는 캐시 배열을 초기화하는 cache_init()라는 함수를 정의한다.
void cache_init()
{
  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++) // 이 줄은 cache_items 배열의 모든 요소를 ​​반복하는 루프를 시작한다. 줄 끝에 있는 세미콜론 때문에 루프가 비어 있다. 즉, 루프가 아무 작업도 수행하지 않음을 의미합니다.
  {
    // 아래 코드는 각 캐시 항목을 기본값으로 초기화한다. 캐시 항목의 우선 순위를 0으로 설정하여 아직 액세스하지 않았음을 나타낸다. 또한 is_empty 플래그를 1로 설정하여 캐시 항목이 비어 있음을 나타낸다. 'read_count'는 0으로 초기화되어 현재 캐시 항목을 읽고 있는 스레드가 없음을 나타낸다. 그런 다음 두 개의 세마포어가 초기화된다. 'wmutex'는 캐시 항목에 대한 쓰기 액세스를 제어하는 ​​데 사용되고 'rdcntmutex'는 캐시 항목에 대한 읽기 액세스를 제어하는 ​​데 사용된다.
    cache.cache_items[i].priority = 0;
    cache.cache_items[i].is_empty = 1;
    cache.cache_items[i].read_count = 0;
    Sem_init(&cache.cache_items[i].wmutex, 0, 1);
    Sem_init(&cache.cache_items[i].rdcntmutex, 0, 1);
  }
}

// 이것은 캐시에 액세스할 때 동기화를 처리하는 C 프로그래밍 언어의 코드 조각입니다. 
void read_before(int index) // 인덱스를 매개변수로 취하는 함수 선언
{
  // P 함수를 통해 rdcntmutex에 접근 가능하게 해줌
  P(&cache.cache_items[index].rdcntmutex); // 주어진 인덱스에서 캐시 항목과 관련된 "rdcntmutex"라는 뮤텍스에 대한 잠금을 획득합니다. 'P'함수는 일반적으로 잠금을 획득하는 데 사용되며 잠금이 해제될 때까지 다른 스레드가 공유 리소스에 액세스 하지 못하도록 합니다.

  // 사용하지 않는 애라면 0에서 1로 바뀌었을 것
  cache.cache_items[index].read_count += 1; // 이 줄은 주어진 인덱스에서 캐시 항목과 관련된 읽기 수를 1씩 증가시킵니다. 읽기 수는 현재 읽고 있는 스레드 수를 추적하는 데 사용됩니다. 캐시 항목에서.

  // 1일 때만 캐쉬에 접근 가능, 누가 쓰고 있는 애라면 2가 되기 때문에 if문으로 들어올 수 없음
  if (cache.cache_items[index].read_count == 1) // 이 라인은 주어진 인덱스에서 캐시 항목과 관련된 읽기 횟수가 1인지 확인합니다. 스레드는 현재 캐시 항목에서 읽고 있습니다.
  {
    // wmutex에 접근 가능하게 해줌, 즉 캐시에 접근
    P(&cache.cache_items[index].wmutex); // 이 줄은 주어진 인덱스에서 캐시 항목과 관련된 "wmutex"라는 뮤텍스에 대한 잠금을 획득합니다. 이 잠금은 캐시 항목을 읽는 동안 다른 스레드가 캐시 항목을 수정하지 못하도록 방지하는 데 사용됩니다.
  }

  // V 함수를 통해 접근 불가능하게 해줌
  V(&cache.cache_items[index].rdcntmutex); // 이 줄은 주어진 인덱스에서 캐시 항목과 관련된 "rdcntmutex" 뮤텍스에 대한 잠금을 해제합니다. 'V' 함수는 일반적으로 다른 스레드가 공유 리소스에 액세스할 수 있도록 잠금을 해제하는 데 사용됩니다.
}

void read_after(int index)
{
  P(&cache.cache_items[index].rdcntmutex); // 이 행은 P 함수를 사용하여 지정된 인덱스의 캐시 항목에 대한 rdcntmutex 뮤텍스에 대한 잠금을 획득한다. 이는 현재 스레드가 캐시 항목에서 읽는 동안 다른 스레드가 이 캐시 항목에 대한 read_count 변수를 수정하지 않도록 하기 위해 필요하다.
  cache.cache_items[index].read_count -= 1; // 이 줄은 현재 스레드가 읽기를 완료했기 때문에 지정된 인덱스에서 캐시 항목에 대한 read_count 변수를 감소시킨다.

  if (cache.cache_items[index].read_count == 0) // 이 줄은 지정된 인덱스의 캐시 항목에 대한 read_count 변수가 0에 도달했는지 확인한다. 그렇다면 현재 더 이상 스레드가 이 캐시 항목에서 읽고 있지 않으며 스레드가 캐시 항목에 쓰기 위해 wmutex 뮤텍스를 획득하는 것이 안전함을 의미한다.
  {
    V(&cache.cache_items[index].wmutex); // 이 줄은 V 함수를 사용하여 지정된 인덱스의 캐시 항목에 대한 wmutex 뮤텍스의 잠금을 해제한다. 이를 통해 스레드는 wmutex 뮤텍스를 획득하고 캐시 항목에 쓸 수 있다.
  }

  V(&cache.cache_items[index].rdcntmutex); // 이 행은 V 함수를 사용하여 지정된 인덱스의 캐시 항목에 대한 rdcntmutex 뮤텍스의 잠금을 해제한다. 이렇게 하면 다른 스레드가 캐시 항목에서 읽을 수 있다.
}

int cache_find(char *url) // 문자열 매개변수 url을 취하고 정수 값을 반환하는 cache_find 함수를 정의한다.
{
  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++) // 캐시의 모든 항목을 반복하는 for 루프를 시작한다.
  {
    read_before(i); // 코드 스니펫에 정의되지 않은 read_before 함수를 호출하고 진행하기 전에 인덱스 i의 캐시 항목에 대해 일종의 읽기 작업을 수행하는 것으로 추정된다.

    // 캐시가 비어있고, 해당 url이 이미 캐시에 들어있을 경우
    // is_empty == 1 일 때 캐시가 비어있는거 아닌가?
    if (cache.cache_items[i].is_empty == 0 && strcmp(url, cache.cache_items[i].cache_url) == 0) // 인덱스 i의 캐시 항목이 비어 있지 않은지 확인하고 url은 캐시 항목의 cache_url과 일치한다면
    {
      read_after(i); // 코드 스니펫에 정의되지 않은 read_after 함수를 호출하고 아마도 비교가 완료된 후 인덱스 i의 캐시 항목에 대해 일종의 읽기 작업을 수행한다.
      return i;      // 캐시에서 찾은 경우 캐시 항목의 인덱스를 반환한다.
    }

    read_after(i);
  }

  return -1; // for 루프 반복이 완료된 후 캐시에서 URL을 찾을 수 없으면 -1을 반환한다.
}
// 코드스니펫 : 특정 기능을 수행하거나 특정 기술을 보여주는 프로그래밍 언어 명령 또는 소스 코드의 작은 부분입니다. 특정 프로그래밍 문제를 해결하는 방법에 대한 예나 그림으로 자주 사용되거나 프로그래밍 언어의 일반적인 작업에 대한 빠른 참조로 사용됩니다

// cache_eviction: 캐시에 공간이 필요하여 데이터를 지우는 함수
int cache_eviction()
{
  int min = LRU_PRIORITY; // min을 캐시 제거를 위해 가능한 최대 우선순위 값인 LRU_PRIORITY로 초기화한다.
  int index = 0;          // index를 0으로 초기화한다. 캐시 항목이 모두 차면 반환된다.

  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++) // 캐시 배열의 각 캐시 항목을 반복한다.
  {
    read_before(i); // 읽기 액세스를 위해 잠글 현재 인덱스의 캐시 항목에 대해 read_before() 함수를 호출한다.

    if (cache.cache_items[i].is_empty == 1) // 현재 캐시 항목이 비어 있으면 
    {
      // 인덱스를 현재 인덱스로 설정하고 루프를 종료한다.
      index = i;
      read_after(i);
      break;
    }

    if (cache.cache_items[i].priority < min) // 현재 캐시 항목의 우선순위가 현재 최소값보다 낮은 경우 
    {
      // 최소값을 현재 항목의 우선순위로, 인덱스를 현재 인덱스로 업데이트한다.
      index = i;
      min = cache.cache_items[i].priority;
      read_after(i); 
      continue;
    }

    read_after(i); // 현재 인덱스의 캐시 항목에 대해 read_after() 함수를 호출하여 읽기 액세스를 잠금 해제한다.
  }

  return index; // 제거할 캐시 항목의 인덱스를 반환한다. 비어 있는 캐시 항목 또는 가장 최근에 사용한 항목이다.
}

// cache_LRU: 현재 캐시의 우선순위를 모두 올림, 최근 캐시 들어갔으므로
void cache_LRU(int index) // index라는 정수 매개변수를 사용하는 cache_LRU라는 함수의 함수 시그니처이다.
{
  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++) // 캐시의 각 항목을 반복하는 for 루프이다. 인덱스 0에서 시작하여 i가 MAX_OBJECT_SIZE_IN_CACHE보다 작은 동안 계속된다. 루프는 각 반복 후 'i'를 1씩 증가시킨다.
  {
    if (i == index) {// 이 if 문은 현재 i 값이 제공된 index 값과 같은지 확인한다. 그렇다면 'continue' 키워드는 루프 본문에서 더 이상 코드를 실행하지 않고 루프의 다음 반복으로 건너뛰는 데 사용된다.
      continue;
    }

    P(&cache.cache_items[i].wmutex); //  이 줄은 P 함수를 사용하여 현재 캐시 항목에 대한 쓰기 뮤텍스를 획득한다. 이는 한 번에 하나의 스레드만 지정된 캐시 항목에 액세스할 수 있도록 하는 동기화 메커니즘이다.

    if (cache.cache_items[i].is_empty == 0) //  이 if 문은 현재 캐시 항목이 비어 있지 않은지 확인한다. 
    {
      cache.cache_items[i].priority -= 1; // 그렇지 않은 경우 캐시 항목의 우선 순위 값이 1씩 감소한다. 이는 LRU(최소 최근 사용) 캐시 제거 전략의 일부이며 우선 순위 값이 낮은 항목이 더 최근에 사용된 것으로 간주된다.
    }

    V(&cache.cache_items[i].wmutex); // 이 줄은 V 함수를 사용하여 현재 캐시 항목에 대한 쓰기 뮤텍스를 해제한다. 이렇게 하면 필요한 경우 다른 스레드가 캐시 항목에 액세스할 수 있다.
  }
}

// cache_uri: 빈 캐시를 찾아 값을 넣어주고 나머지 캐시의 우선순위를 내려주는 함수
void cache_uri(char *uri, char *buf) // uri라는 문자 배열에 대한 포인터와 buf라는 문자 배열에 대한 포인터인 두 개의 인수가 있는 함수 정의.
{
  int index = cache_eviction(); // cache_eviction() 함수를 호출하고 반환 값을 index라는 정수 변수에 할당힌다.
  P(&cache.cache_items[index].wmutex); // 인덱스 위치에서 캐시 항목과 연결된 이진 세마포어를 기다리므로 한 번에 하나의 스레드만 이 항목에 액세스할 수 있다.
  strcpy(cache.cache_items[index].cache_object, buf); // index 위치에 있는 캐시 항목의 cache_object 배열에 buf 배열의 내용을 복사한다.
  strcpy(cache.cache_items[index].cache_url, uri);    // uri 배열의 내용을 index 위치에 있는 캐시 항목의 cache_url 배열로 복사한다.
  cache.cache_items[index].is_empty = 0;              // index 위치에 있는 캐시 항목의 is_empty 필드를 0으로 설정하여 항목이 현재 사용 중임을 나타낸다.
  cache.cache_items[index].priority = 9999;           // 위치에 있는 캐시 항목의 priority 필드를 최대값으로 설정하여 가장 최근에 사용한 항목임을 나타낸다.
  cache_LRU(index);                                   // index 위치를 인수로 사용하여 cache_LRU() 함수를 호출하여 다른 모든 캐시 항목의 우선 순위 값을 업데이트한다.

  V(&cache.cache_items[index].wmutex); // 인덱스 위치에서 캐시 항목과 연결된 이진 세마포어에 신호를 보내 다른 스레드에서 해당 항목에 액세스할 수 있음을 나타낸다.
}

//이진 세마포어 : 컴퓨터 과학에서 이진 세마포어는 전통적으로 0과 1의 두 가지 값만 가질 수 있는 동기화 프리미티브입니다.공유 리소스에 대한 액세스를 관리 및 제어하거나 다중 스레드 또는 다중 프로세스 환경에서 프로세스 동기화를 구현하는 데 사용됩니다. 바이너리 세마포어에는 대기 및 신호라는 두 가지 주요 작업이 있습니다.대기 작업은 세마포어 값을 감소시키고 값이 0이면 호출 프로세스를 차단하고 그렇지 않으면 실행을 계속합니다.신호 연산은 세마포어 값을 증가시키고 세마포어를 기다리는 차단된 프로세스를 깨웁니다. 바이너리 세마포어는 코드의 중요한 섹션에 대한 액세스를 제어하고 공유 데이터 구조를 동시 액세스로부터 보호하며 프로세스 또는 스레드 간의 경쟁 조건을 방지하기 위해 운영 체제 및 임베디드 시스템 프로그래밍에서 자주 사용됩니다.또한 컴퓨터 네트워크 및 분산 시스템과 같은 컴퓨터 과학의 다른 영역에서도 사용됩니다.