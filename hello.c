#include "csapp.h"
#include <pthread.h>
typedef void *(func)(void *);
// 쓰레드는 pthread_create함수를 호출해서 다른 쓰레드를 생성한다.
// pthread_create함수는 새 쓰레드를 만들고 쓰레드 루틴 arg를 새 쓰레드의 컨텍스트 내에서 입력 인자 attr를 가지고 실행된다. 인자는 새롭게 만들어진 쓰레드의 기본 성질을 바꾸기 위해 사용될 수 있다. 이 성질들을 바꾸는 것은 이 책의 영역을 벗어나며, 우리의 예제에서는 항상 pthread_create을 NULL attr인자로 호출한다.
// pthread_create이 리턴할 때, 인자 tid는 새롭게 만들어진 쓰레드의 ID를 갖는다. 
int pthread_create(pthread_t *tid, pthread_attr_t *attr,
                   func *f, void *arg);

// 새 쓰레드는 pthread_self함수를 호출해서 자신의 쓰레드 ID를 결정할 수 있다. 
pthread_t pthread_self(void);

// 쓰레드는 다음의 한 가지 방법으로 종료한다.:
// 쓰레드는 자신의 취상위 쓰레드 루틴이 리턴할 때 묵시적으로 종료한다.
// 쓰레드는 pthread_exit함수를 호출해서 명시적으로 종료한다. 만일 메인 쓰레드가 pthread_eixt를 호출하면 다른 모든 쓰레드가 종료하기를 기다리고, 그 후에 메인 쓰레드와 전체 프로세스를 thread_return 리턴 값으로 종료한다.
// 일부 피어 쓰레드는 리눅스 함수를 호출하며, 이것은 프로세스와 이 프로세스에 관련된 쓰레드 모두를 종료한다.
void pthread_exit(void *thread_return);

// 다른 피어 쓰레드는 pthread_cancle함수를 현재 쓰레드의 ID로 호출해서 현재 쓰레드를 종료한다.
int pthread_cancel(pthread_t tid);

// 종료한 쓰레드의 삭제 Reaping
// 쓰레드는 pthread_join함수를 호출해서 다른 쓰레드가 종료하기를 기다린다.
// pthread_join함수는 쓰레드 tid가 종료할 때까지 멈춰 있으며, 쓰레드 루틴이 리턴한 기본 (void*) 포인터를 thread_return이 가리키는 위치로 할당하고, 그 후에 종료된 쓰레드가 가지고 있던 모든 메모리 자원을 삭제한다.
// 리눅스의 wait함수와는 달리 pthread_join함수는 특정 쓰레드가 종료하기를 기다려야 한다. 이 임의의 쓰레드가 종료하는 것을 기다리도록 지정할 수 있는 방법은 없다. pthread_join으로 인해서 프로세스의 종료를 감지하기 위해서는 덜 직관적인 다른 메커니즘을 사용해야 한다. 실제로 Stevens는 이것을 명세서의 버그라고 설득력 있게 주장한다.
int pthread_join(pthread_t tid, void **thread_return);

// 쓰레드 분리하기
// 언제나 쓰레드는 연결 가능(joinable)하거나 분리되어(detached)있다. 연결 가능한 쓰레드는 다른 쓰레드에 의해 청소되고 종료될 수 있다. 자신의 메모리 자원들(스택과 같은)은 다른 쓰레드에 의해 청소될 때까지는 반환되지 않는다. 반대로, 분리된 쓰레드는 다른 쓰레드에 의해서 청소되거나 종료될 수 없다. 자신의 메모리 자원들은 이 쓰레드가 종료할 때 시스템에 의해 자동으로 반환된다.
// 기본적으로 쓰레드는 연결 가능하도록 생성된다. 메모리 누수를 막기 위해서 각각의 연결 가능 쓰레드는 다른 쓰레드에 의해 명시적으로 소거되거나, pthread_detach함수를 호출해서 분리되어야 한다.
// pthread_detach함수는 연결 가능한 쓰레드 tid를 분리한다. 쓰레드들은 pthread_detach 인자로 pthread_self()를 사용해서 자기 자신을 분리할 수 있다.
//실제 프로그램에서 분리된 쓰레드를 사용해야하는 타당한 이유: 예를 들어, 고성능 웹 서버는 웹 브라우저로부터 연결 요청을 수신할 때마다 새로운 피어 쓰레드를 생성할 수 있다.
//각각의 연결이 별도의 쓰레드에 의해서 독립적으로 처리되기 때문에 서버가 명시적으로 각각의 피어 쓰레드가 종료하기를 기다리는 것은 불푤이효다. 그리고 실제로 바람직하지 않다.
// 각 피어 쓰레드는 요청 처리전에 자신을 분리해야 하며, 자신의 메모리 자원들이 종료 후에 반환될 수 있도록 하낟.
int pthread_detach(pthread_t tid);

// 쓰레드 초기화
// pthread_once함수는 쓰레드 루틴에 관련된 상태를 초기화할 수 있도록 한다.
// once_control 변수는 전역 또는 정적 변수로 항상 PTHREAD_ONCE_INIT으로 초기화된다. pthread_once를 once_control 인자로 처음 호출하면 init_routine을 호출하며, 이 함수는 아무것도 리턴하지 않으며 인자도 없는 함수다. pthread_once함수는 동적으로 다수의 쓰레드에 의해 공유된 전역 변수들을 초기화할 필요가 있을 때 유용하다.
pthread_once_t once_control = PTHREAD_ONCE_INIT;
int pthread_once(pthread_once_t *once_control,
                 void (*init_routine)(void));

void *thread(void *vargp); //프로토타입이 나타낸 것처럼 각각의 쓰레드 루틴은 입력으로 한 개의 기본 포인터를 가져오고, 기본 포인터를 리턴한다. 만일 다수의 인자를 어떤 쓰레드 루틴으로 전달하려고 하면, 이 인자들을 구조체에 넣고 포인터를 구조체로 전달한다. 
int main() // 메인 쓰레드를 위한 코드의 시작 부분을 표시
{
  pthread_t tid; // 메인 쓰레드는 한 개의 지역변수 tid를 선언하고, 이것은 피어 쓰레드의 쓰레드 ID를 저장하기 위해 사용될 것이다.
  Pthread_create(&tid, NULL, thread, NULL); // 메인 쓰레드는 pthread_create함수를 호출해서 새로운 피어 쓰레드를 생성했다. pthread_create으로의 호출이 리턴할 때, 메인 쓰레드와 새롭게 생성된 피어 쓰레드는 동시에 돌고 있으며, tid는 새로운 쓰레드의 ID를 포함하고 있다.
  Pthread_join(tid, NULL); // 메인 쓰레드는 피어 쓰레드가 pthread_join을 호출해서 종료하기를 기다린다. 
  exit(0); // 마지막으로 메인 쓰레드는 exit를 호출하고, 이것은 현재 프로세스에서 돌고 있는 모든 쓰레드(이 경우에는 단순히 메인 쓰레드를)를 종료한다.
}

// 피어 쓰레드 루틴을 정의한다. 스트링을 간단히 인쇄하고, 그 후에 
void *thread(void *vargp) /* Thread routine */
{
  printf("Hello, world!\n");
  return NULL; // 리턴 문장을 실행해서 피어 쓰레드를 종료한다.
}