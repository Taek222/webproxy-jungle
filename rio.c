#include "csapp.h"
ssize_t rio_readn(int fd, void *usrbuf, size_t n); // rio_readn함수는 식별자 fd의 현재 파일 위치에서 메모리 위치 usrbuf로 최대 n 바이트를 전송한다.
// rio_readn함수는 EOF를 만나면 짧은 카운트만으르 리턴한다. 
ssize_t rio_writen(int fd, void *usrbuf, size_t n); // 비슷하게, rio_writen함수는 usrbuf에서 식별자 fd로 n바이트를 전송한다.
//rio_writen함수는 절대로 짧은 카운트를 리턴하지 않는다.
// rio_readn와 rio_writen으로의 호출은 같은 식별자에 대해서 임의로 중첩될 수 있다.
Returns : number of bytes transferred if OK, 0 on EOF(rio_readn only), −1 on error

//각 함수는 응용 시그널 핸들러에서 리턴해서 중단된다면, read나 write함수를 수동으로 재시작하는 것에 유의히라.이러한 함수들을 가능하게 하기 위해서 중단된 시스템 콜을 허용하고, 필요한 경우에 이들을 재시작할 수 있도록 한다.
ssize_t rio_readn(int fd, void *usrbuf, size_t n) 2
{
  size_t nleft = n;
  ssize_t nread;
  char *bufp = usrbuf;
  while (nleft > 0)
  {
    if ((nread = read(fd, bufp, nleft)) < 0)
    {
      if (errno == EINTR) /* Interrupted by sig handler return */
          nread = 0;     /* and call read() again */
      else
        return -1; /* errno set by read() */
    }
    else if (nread == 0)
      break; /* EOF */
    nleft -= nread;
    bufp += nread;
  }
  return (n - nleft); /* Return >= 0 */
}

ssize_t rio_writen(int fd, void *usrbuf, size_t n) 2
{
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;
  while (nleft > 0)
  {
    if ((nwritten = write(fd, bufp, nleft)) <= 0)
    {
      if (errno == EINTR) /* Interrupted by sig handler return */
          nwritten = 0;  /* and call write() again */
      else
        return -1; /* errno set by write() */
    }
    nleft -= nwritten;
    bufp += nwritten;
  }
  return n;
}

// 텍스트 라인 전체를 내부 읽기 버퍼에서 복사하는 래퍼 함수(rio_readlineb)를 호출하는 것으로, 이것은 버퍼가 비워지게 될 때마다 버퍼를 다시 채우기 위해 자동으로 read를 호출한다. 텍스트 라인과 바이너리 데이터를 포함하는 파일에 대해서 rio_readn의 버퍼형 버전인 rio_readnb를 제공하며, 이것은 rio_readlineb와 동일한 읽기 버퍼에서 원시 바이트들을 전송한다.
void rio_readinitb(rio_t *rp, int fd); // rio_readinitib함수는 open한 식별자마다 한 번 호출된다. 이 함수는 식별자 fd를 주소에 위치한 rio_t 타입의 읽기 버퍼와 연결한다.
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen); // rio_readlineb함수는 다음 텍스트 줄을 파일 rp(종료 새 줄 문자를 포함ㅎ해서)에서 읽고, 이것을 메모리 위치 usrbuf로 복사하고, 텍스트 라인을 널(0)문자로 종료시킨다.
// rio_readlineb함수는 최대 maxlen-1개의 바이트를 읽으며, 종료용 널 문자를 위한 공간을 남겨둔다. maxlen-1 바이트를 넘는 텍스트 라인들은 잘라내서 널 문자로 종료시킨다.
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n); // rio_readnb함수는 최대 n바이트를 파일 rp로부터 메모리 위치 usrbuf로 읽는다. rio_readlineb와 rio_readn로의 호출은 동일한 식별자에 대해서 임의로 중첩될 수 있다. 그러나 이 버퍼를 사용하는 함수들은 버퍼가 없는 rio_readn함수로의 호출과 중첩되어서는 안된다.
Returns : number of bytes read if OK, 0 on EOF, −1 on error


// 텍스트 파일을 표준 입력에서 출력으로, 한 번에 한 줄씩 Rio함수들을 사용해서 복사하는 방법을 보여준다.
int main(int argc, char **argv)
{
  int n;
  rio_t rio;
  char buf[MAXLINE];
  Rio_readinitb(&rio, STDIN_FILENO);
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
    Rio_writen(STDOUT_FILENO, buf, n);
}

// 읽기 버퍼의 포맷을 초기화 함수 rio_eadinitb함수와 함께 보여준다.
// rio_readinitb함수는 한 개의 빈 버퍼를 설정하고, 이 버퍼와 한 개의 오픈한 파일 식별자를 연결한다.
#define RIO_BUFSIZE 8192
typedef struct
{
  int rio_fd;                /* Descriptor for this internal buf */
  int rio_cnt;               /* Unread bytes in internal buf */
  char *rio_bufptr;          /* Next unread byte in internal buf */
  char rio_buf[RIO_BUFSIZE]; /* Internal buffer */
} rio_t;

void rio_readinitb(rio_t *rp, int fd)
{
  rp->rio_fd = fd;
  rp->rio_cnt = 0;
  rp->rio_bufptr = rp->rio_buf;
}


// Rio 읽기 루틴의 핵심은 rio_read함수다. rio_read함수는 리눅스 read함수의 버퍼형 버전이다. rio_read가 n 바이트를 읽기 위한 요청으로 호출되면, rp->rio_cnt개의 읽지 않은 바이트가 읽기 버퍼 내에 존재한다. 만일 버퍼가 비워지면, read를 호출해서 다시 채워 놓는다. 이 read함수를 호출한 결과로 짧은 카운트를 넘겨받으면, 이것은 에러가 아니며, 단순히 읽기 버퍼의 일부가 채워진 효과를 갖는다.일단 버퍼가 비어 있지 않으므로 rio_read는 n과 rp->rio_cnt 중의 최소값을 바이트 수로 하여 읽기 버퍼에서 사용자 버퍼로 복사하고 복사한 바이트 수를 리턴한다.
// 응용프로그램에게 rio_read함수는 리눅스 read함수와 동일한 의미를 가진다. 에러 발생 시 -1을 리턴하고, errno를 적절히 설정한다. EOF 시에는 0을 리턴한다.만일 요청한 바이트 수가 읽기 버퍼 내에 읽지 않는 바이트 수보다 더 크면 짧은 카운트를 리턴한다. 이 두 함수의 유사점은 rio_read를 read 대신에 사용하면 여러 종류의 버퍼형 읽기 함수를 만들기가 쉽다는 것이다. 
static ssize_t rio_read(rio_t *rp, char *usrbuf, size_t n)
{
  int cnt;
  while (rp->rio_cnt <= 0)
  { /* Refill if buf is empty */
    rp->rio_cnt = read(rp->rio_fd, rp->rio_buf,
                         sizeof(rp->rio_buf));
    if (rp->rio_cnt < 0)
    {
      if (errno != EINTR) /* Interrupted by sig handler return */
          return -1;
    }
    else if (rp->rio_cnt == 0) /* EOF */
        return 0;
    else rp->rio_bufptr = rp->rio_buf; /* Reset buffer ptr */
  }
  /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
      cnt = n;
  if (rp->rio_cnt < n)
    cnt = rp->rio_cnt;
  memcpy(usrbuf, rp->rio_bufptr, cnt);
  rp->rio_bufptr += cnt;
  rp->rio_cnt -= cnt;
  return cnt;
}

// rio_readlineb 루틴은 rio_read를 최대 maxlen-1회 호출한다. 각각의 호출은 읽기 버퍼에서 1바이트를 리턴하고, 그 후에 종료하는 새 줄인지 체크한다.
ssize_t rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen)
{
  int n, rc;
  char c, *bufp = usrbuf;
  for (n = 1; n < maxlen; n++)
  {
    if ((rc = rio_read(rp, &c, 1)) == 1)
    {
        *bufp++ = c;
        if (c == ’\n’)
        {
          n++;
          break;
        }
    }
    else if (rc == 0)
    {
        if (n == 1) return 0; /* EOF, no data read */
        else break;           /* EOF, some data was read */
    }
    else return -1; /* Error */
  }
  *bufp = 0;
  return n - 1;
}

// rio_readnb함수는 rio_read로 read를 대체해서 rio_readn과 동일한 구조를 가진다. 비슷하기 
ssize_t rio_readnb(rio_t *rp, void *usrbuf, size_t n) 2
{
  size_t nleft = n;
  ssize_t nread;
  char *bufp = usrbuf;
  while (nleft > 0)
  {
    if ((nread = rio_read(rp, bufp, nleft)) < 0) return -1; /* errno set by read() */
    else if (nread == 0) break;                           /* EOF */
    nleft -= nread;
    bufp += nread;
  }
  return (n - nleft); /* Return >= 0 */
}