#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

// 구조체 정의: 검색 위치를 저장할 연결 리스트 노드
typedef struct node {
    int index;
    struct node* next;
} Node;

// 전역 변수
int fd;                         // 입력 파일 디스크립터
char *target;                   // 탐색할 문자열 (명령행 인자)
int targetLength;               // target 문자열의 길이
int globalIndex = 0;            // 파일의 텍스트 부분에서 읽은 문자 개수 (헤더 이후부터)
int count = 0;                  // 현재까지 일치한 문자 수
int potential_start = 0;        // 현재 일치가 시작된 파일 내 인덱스
Node *head = NULL;              // 결과(찾은 위치)를 저장할 연결 리스트의 head
Node *tail = NULL;              // 연결 리스트의 tail

// 뮤텍스 선언: 파일 읽기 및 매칭 상태 보호, 그리고 결과 리스트 보호
pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

// 모든 쓰레드가 파일 디스크립터에서 한 문자씩 읽으면서 탐색하는 함수
void* thread_func(void* arg) {
    char c;
    ssize_t n;
    while (1) {
        // 파일 읽기 및 매칭 상태 업데이트를 위해 뮤텍스 획득
        pthread_mutex_lock(&read_mutex);
        n = read(fd, &c, 1);
        if(n != 1) {
            // 파일의 끝이나 에러인 경우 뮤텍스 해제 후 종료
            pthread_mutex_unlock(&read_mutex);
            break;
        }
        int pos = globalIndex;
        globalIndex++;
        
        // 현재 문자 c와 target 문자열의 다음 문자 비교
        if(c == target[count]) {
            if(count == 0) {
                potential_start = pos;  // 매칭 시작 인덱스 기록
            }
            count++;
            if(count == targetLength) {
                // target 문자열과 완전히 일치하면 결과 연결 리스트에 추가
                pthread_mutex_lock(&list_mutex);
                Node* newNode = (Node*)malloc(sizeof(Node));
                if(newNode == NULL) {
                    perror("malloc");
                    exit(EXIT_FAILURE);
                }
                newNode->index = potential_start;
                newNode->next = NULL;
                if(head == NULL) {
                    head = newNode;
                    tail = newNode;
                } else {
                    tail->next = newNode;
                    tail = newNode;
                }
                pthread_mutex_unlock(&list_mutex);
                // 일치 후에는 count를 0으로 초기화 (단순 구현 – overlapping match는 고려하지 않음)
                count = 0;
            }
        } else {
            // 불일치: 현재 문자가 target의 첫 문자와 일치하면 새 매칭 시작으로 처리
            if(c == target[0]) {
                potential_start = pos;
                count = 1;
            } else {
                count = 0;
            }
        }
        pthread_mutex_unlock(&read_mutex);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    // 명령행 인자: {프로그램명} {substring} {number of parallel threads} {input file} {output file}
    if(argc != 6) {
        fprintf(stderr, "Usage: %s <substring> <number_of_threads> <input_file> <output_file>\n", argv[0]);
        return -1;
    }

    target = argv[1];
    targetLength = strlen(target);
    int threadCount = atoi(argv[2]);
    char *inputFilename = argv[3];
    char *outputFilename = argv[4];

    // 입력 파일 열기 (읽기 전용)
    fd = open(inputFilename, O_RDONLY);
    if(fd < 0) {
        perror("open input file");
        exit(EXIT_FAILURE);
    }

    // 입력 파일의 첫 부분에는 0~99999 사이의 숫자(문자열 개수)가 있으므로 헤더를 스킵한다.
    // 한 문자씩 읽어 '\n'이 나올 때까지 진행.
    char ch;
    while(1) {
        ssize_t n = read(fd, &ch, 1);
        if(n != 1)
            break;
        if(ch == '\n')
            break;
    }
    // 헤더 이후부터 탐색 시작: globalIndex는 0부터 시작

    // 쓰레드 생성
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * threadCount);
    if(threads == NULL) {
        perror("malloc threads");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < threadCount; i++) {
        if(pthread_create(&threads[i], NULL, thread_func, NULL) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < threadCount; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);
    close(fd);

    // 결과 리스트는 파일에서 읽은 순서대로(즉, 오름차순) 저장되어 있으므로 그대로 출력 파일에 기록
    FILE *ofp = fopen(outputFilename, "w");
    if(ofp == NULL) {
        perror("open output file");
        exit(EXIT_FAILURE);
    }
    Node *cur = head;
    while(cur != NULL) {
        fprintf(ofp, "%d\n", cur->index);
        Node *temp = cur;
        cur = cur->next;
        free(temp);
    }
    fclose(ofp);

    return 0;
}
