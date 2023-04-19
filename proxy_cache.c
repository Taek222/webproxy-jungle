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

typedef struct
{
  char cache_object[MAX_OBJECT_SIZE];
  char cache_url[MAXLINE];
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
  pthread_t tid;

  cache_init();
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  Signal(SIGPIPE, SIG_IGN);
  listenfd = Open_listenfd(argv[1]);

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    Pthread_create(&tid, NULL, thread, (void *)connfd);
  }
}
void *thread(void *vargsp)
{
  int connfd = (int)vargsp;
  Pthread_detach(Pthread_self());
  doit(connfd);
  Close(connfd);
}

void doit(int connfd)
{
  int serverfd, port;
  char server_header[MAXLINE];
  char buf[MAXLINE], cachebuf[MAX_OBJECT_SIZE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], url[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE];
  rio_t clientrio, serverrio;
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

  if (!(check_cache(url, uri, connfd)))
  {
    return;
  }
  parse_uri(uri, hostname, &port, path);
  make_header(server_header, hostname, path, &clientrio);

  serverfd = server_connection(hostname, port);

  Rio_readinitb(&serverrio, serverfd);
  Rio_writen(serverfd, server_header, strlen(server_header));

  size_t response;
  int bufsize = 0;
  while ((response = Rio_readlineb(&serverrio, buf, MAXLINE)) != 0)
  {
    bufsize += response;
    if (bufsize < MAX_OBJECT_SIZE)
    {
      strcat(cachebuf, buf);
    }

    Rio_writen(connfd, buf, response);
  }

  Close(serverfd);

  if (bufsize < MAX_OBJECT_SIZE)
  {
    cache_uri(url, cachebuf);
  }
}

int check_cache(char *url, char *uri, int connfd)
{
  strcpy(url, uri);
  int index;

  if ((index = cache_find(url)) != -1)
  {
    read_before(index);

    Rio_writen(connfd, cache.cache_items[index].cache_object, strlen(cache.cache_items[index].cache_object));

    read_after(index);

    return 0;
  }

  return 1;
}
void make_header(char *final_header, char *hostname, char *path, rio_t *client_rio)
{
  char buf[MAXLINE];
  char request_header[MAXLINE], host_header[MAXLINE], etc_header[MAXLINE];

  sprintf(request_header, "GET %s HTTP/1.0\r\n", path);

  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
  {
    if (strcmp(buf, "\r\n") == 0)
    {
      break;
    }

    if (!strncasecmp(buf, "Host", strlen("Host")))
    {
      strcpy(host_header, buf);
      continue;
    }

    if (strncasecmp(buf, "User-Agent", strlen("User-Agent")) && strncasecmp(buf, "Connection", strlen("Connection")) && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")))
    {
      // 위 세가지 헤더 이외의 다른 헤더가 요청되었을 때, 따로 저장하여 전달
      strcat(etc_header, buf);
    }
  }

  if (strlen(host_header) == 0)
  {
    sprintf(host_header, "Host: %s\r\n", hostname);
  }

  sprintf(final_header, "%s%s%s%s%s%s%s",
          request_header,
          host_header,
          "Connection: close\r\n",
          "Proxy-Connection: close\r\n",
          user_agent_hdr,
          etc_header,
          "\r\n");
}

inline int server_connection(char *hostname, int port)
{
  char portStr[100];

  // Open_clientfd 함수는 port를 문자 파라미터로 넣어야 함
  sprintf(portStr, "%d", port);

  return Open_clientfd(hostname, portStr);
}
void parse_uri(char *uri, char *hostname, int *port, char *path)
{
  char *first = strstr(uri, "//");
  first = first != NULL ? first + 2 : uri;

  char *next = strstr(first, ":");

  *port = 80;
  if (next)
  {
    *next = '\0';
    sscanf(first, "%s", hostname);
    sscanf(next + 1, "%d%s", port, path);
  }
  else
  {
    next = strstr(first, "/");

    if (next)
    {
      *next = '\0';
      sscanf(first, "%s", hostname);

      *next = '/';
      sscanf(next, "%s", path);
    }
    else
    {
      sscanf(first, "%s", hostname);
    }
  }
}

void cache_init()
{
  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++)
  {
    cache.cache_items[i].priority = 0;
    cache.cache_items[i].is_empty = 1;
    cache.cache_items[i].read_count = 0;
    Sem_init(&cache.cache_items[i].wmutex, 0, 1);
    Sem_init(&cache.cache_items[i].rdcntmutex, 0, 1);
  }
}
void read_before(int index)
{
  // P 함수를 통해 rdcntmutex에 접근 가능하게 해줌
  P(&cache.cache_items[index].rdcntmutex);

  // 사용하지 않는 애라면 0에서 1로 바뀌었을 것
  cache.cache_items[index].read_count += 1;

  // 1일 때만 캐쉬에 접근 가능, 누가 쓰고 있는 애라면 2가 되기 때문에 if문으로 들어올 수 없음
  if (cache.cache_items[index].read_count == 1)
  {
    // wmutex에 접근 가능하게 해줌, 즉 캐시에 접근
    P(&cache.cache_items[index].wmutex);
  }

  // V 함수를 통해 접근 불가능하게 해줌
  V(&cache.cache_items[index].rdcntmutex);
}

void read_after(int index)
{
  P(&cache.cache_items[index].rdcntmutex);
  cache.cache_items[index].read_count -= 1;

  if (cache.cache_items[index].read_count == 0)
  {
    V(&cache.cache_items[index].wmutex);
  }

  V(&cache.cache_items[index].rdcntmutex);
}

int cache_find(char *url)
{
  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++)
  {
    read_before(i);

    // 캐시가 비어있고, 해당 url이 이미 캐시에 들어있을 경우
    if (cache.cache_items[i].is_empty == 0 && strcmp(url, cache.cache_items[i].cache_url) == 0)
    {
      read_after(i);
      return i;
    }

    read_after(i);
  }

  return -1;
}

// cache_eviction: 캐시에 공간이 필요하여 데이터를 지우는 함수
int cache_eviction()
{
  int min = LRU_PRIORITY;
  int index = 0;

  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++)
  {
    read_before(i);

    if (cache.cache_items[i].is_empty == 1)
    {
      index = i;
      read_after(i);
      break;
    }

    if (cache.cache_items[i].priority < min)
    {
      index = i;
      min = cache.cache_items[i].priority;
      read_after(i);
      continue;
    }

    read_after(i);
  }

  return index;
}

// cache_LRU: 현재 캐시의 우선순위를 모두 올림, 최근 캐시 들어갔으므로
void cache_LRU(int index)
{
  for (int i = 0; i < MAX_OBJECT_SIZE_IN_CACHE; i++)
  {
    if (i == index)
    {
      continue;
    }

    P(&cache.cache_items[i].wmutex);

    if (cache.cache_items[i].is_empty == 0)
    {
      cache.cache_items[i].priority -= 1;
    }

    V(&cache.cache_items[i].wmutex);
  }
}

// cache_uri: 빈 캐시를 찾아 값을 넣어주고 나머지 캐시의 우선순위를 내려주는 함수
void cache_uri(char *uri, char *buf)
{
  int index = cache_eviction();
  P(&cache.cache_items[index].wmutex);
  strcpy(cache.cache_items[index].cache_object, buf);
  strcpy(cache.cache_items[index].cache_url, uri);
  cache.cache_items[index].is_empty = 0;
  cache.cache_items[index].priority = 9999;
  cache_LRU(index);

  V(&cache.cache_items[index].wmutex);
}