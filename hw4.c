#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#define NO_EINTR(stmt) while((stmt) < 0 && errno == EINTR);

void get_timestamp();
int check_arguments(int argc, char *argv[], int *consumer_count, int* loop_count, char *input_filename);
void *consumer(void *arg);
void *supplier(void *arg);

typedef struct consumer_properties{
    int consumer_id;
    int loop_count;
}ConsumerProperties;

int semaphores;

int main(int argc, char *argv[]){   
    int consumer_count;
    int loop_count;
    char *input_filename;

    pthread_t supplier_thread;
    pthread_t *consumer_thread;

    ConsumerProperties **consumer_properties;

    input_filename = (char *)malloc(sizeof(char)*100);

    setvbuf(stdout, NULL, _IONBF, 0);

    if(check_arguments(argc, argv, &consumer_count, &loop_count, input_filename) == -1){
        fprintf(stderr, "Usage: ./hw4 -C 10 -N 5 -F inputfilePath\n");
        exit(EXIT_SUCCESS);
    }

    consumer_thread = (pthread_t*)malloc(sizeof(pthread_t) * consumer_count);
    if(consumer_thread == NULL){
        perror("malloc: ");
        exit(EXIT_FAILURE);
    }

    consumer_properties = (ConsumerProperties**)malloc(sizeof(ConsumerProperties*) * consumer_count);
    if(consumer_properties == NULL){
        perror("malloc: ");
        exit(EXIT_FAILURE);
    }

    key_t key = ftok("/tmp", 1);

    semaphores = semget(IPC_PRIVATE, 2, IPC_CREAT|IPC_EXCL|0600);
    if(semaphores == -1){
        perror("semget: ");
        exit(EXIT_FAILURE);
    }

    if(semctl(semaphores, 0, SETVAL, 0) == -1){
        perror("semctl: ");
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < consumer_count; i++){
        consumer_properties[i] = (ConsumerProperties*)malloc(sizeof(ConsumerProperties));
        if(consumer_properties[i] == NULL){
            perror("malloc: ");
            exit(EXIT_FAILURE);
        }
        consumer_properties[i]->consumer_id = i;
        consumer_properties[i]->loop_count = loop_count;

        if(pthread_create(&consumer_thread[i], NULL, consumer, (void*)consumer_properties[i]) != 0){
            perror("pthread_create: ");
            exit(EXIT_FAILURE);
        }
    }

    if(pthread_create(&supplier_thread, NULL, supplier, (void*)input_filename) != 0){
        perror("pthread_create: ");
        exit(EXIT_FAILURE);
    }

    pthread_detach(supplier_thread);

    for(int i = 0; i < consumer_count; i++){
        pthread_join(consumer_thread[i], NULL);
    }

    free(consumer_thread);
    free(input_filename);
    for(int i = 0; i < consumer_count; i++){
        free(consumer_properties[i]);
    }
    free(consumer_properties);

    if(semctl(semaphores, 0, IPC_RMID) < 0){
        perror("semctl2: ");
        exit(EXIT_FAILURE);
    }

    return 0;
}


int check_arguments(int argc, char *argv[], int *consumer_count, int* loop_count, char *input_filename){
    *consumer_count = -1;
    *loop_count = -1;

    if(argc != 7){
        printf("Usage: ./hw4 -C 10 -N 5 -F inputfilePath\n");
        return -1;
    }

    for(int i = 0; i < argc; ++i){
        if(strcmp(argv[i], "-C") == 0){
            *consumer_count = atoi(argv[i+1]);
        }
        else if(strcmp(argv[i], "-N") == 0){
            *loop_count = atoi(argv[i+1]);
        }
        else if(strcmp(argv[i], "-F") == 0){
            strcpy(input_filename, argv[i+1]);
        }
    }

    if(*consumer_count == -1 || *loop_count == -1 || *input_filename == NULL){
        return -1;
    }
    return 0;
}

void *supplier(void *arg){
    char *input_filename = (char*)arg;
    int fd;
    int read_bytes = 1;
    char c;
    int sem1_count;
    int sem2_count;
    char timestamp_buf[26];

    struct sembuf ops[2] = {{0, 1, 0}, {1, 1, 0}};

    fd = open(input_filename, O_RDONLY);
    if(fd < 0){
        perror("open: ");
        pthread_exit(NULL);
    }

    while(read_bytes > 0){
        NO_EINTR(read_bytes = read(fd, &c, 1));
        if(read_bytes > 0){
            if((sem1_count = semctl(semaphores, 0, GETVAL)) < 0){
            perror("semctl: ");
            pthread_exit(NULL);
            }
            if((sem2_count = semctl(semaphores, 1, GETVAL)) < 0){
                perror("semctl: ");
                pthread_exit(NULL);
            }

            if(c != '1' && c != '2'){
                fprintf(stderr, "Invalid input file\n");
                exit(EXIT_SUCCESS);
            }
            
            get_timestamp(timestamp_buf);
            printf("%s Supplier: read from input a '%c'. Current amounts: %d x ‘1’, %d x ‘2’.\n", timestamp_buf, c, sem1_count, sem2_count);
            
            if(c == '1'){
                semop(semaphores, &ops[0], 1);
            }
            else if(c == '2'){
                semop(semaphores, &ops[1], 1);
            }

            get_timestamp(timestamp_buf);
            printf("%s Supplier: delivered a ‘%c’. Post-delivery amounts: %d x ‘1’, %d x ‘2’.\n", timestamp_buf, c, sem1_count, sem2_count);
        }
    }

    close(fd);

    get_timestamp(timestamp_buf);
    printf("%s The Supplier has left.\n", timestamp_buf);
    

    pthread_exit(NULL);
}

void *consumer(void *arg){
    ConsumerProperties *consumer_properties = (ConsumerProperties*)arg;
    int sem1_count;
    int sem2_count;
    struct sembuf ops[2] = {
        {0, -1, 0},
        {1, -1, 0}
    };
    char timestamp_buf[26];

    for(int i = 0; i < consumer_properties->loop_count; ++i){
        if((sem1_count = semctl(semaphores, 0, GETVAL)) < 0){
            perror("semctl: ");
            exit(EXIT_FAILURE);
        }
        if((sem2_count = semctl(semaphores, 1, GETVAL)) < 0){
            perror("semctl: ");
            exit(EXIT_FAILURE);
        }

        get_timestamp(timestamp_buf);
        printf("%s Consumer-%d at iteration %d (waiting). Current amounts: %d x ‘1’, %d x ‘2’.\n", timestamp_buf, consumer_properties->consumer_id, i, sem1_count, sem2_count);

        if(semop(semaphores, ops, 2) == -1){
            perror("semop: ");
            exit(EXIT_FAILURE);
        }

        if((sem1_count = semctl(semaphores, 0, GETVAL)) < 0){
            perror("semctl: ");
            exit(EXIT_FAILURE);
        }
        if((sem2_count = semctl(semaphores, 1, GETVAL)) < 0){
            perror("semctl: ");
            exit(EXIT_FAILURE);
        }

        get_timestamp(timestamp_buf);
        printf("%s Consumer-%d at iteration %d (consumed). Post-consumption amounts: %d x ‘1’, %d x ‘2’.\n", timestamp_buf, consumer_properties->consumer_id, i, sem1_count, sem2_count);
    }

    get_timestamp(timestamp_buf);
    printf("%s Consumer-%d has left.\n", timestamp_buf, consumer_properties->consumer_id);
    
    pthread_exit(NULL);
}

void get_timestamp(char *timestamp_buf){
    time_t start;
    struct tm* tm_info;

    time(&start);
    tm_info = localtime(&start);
    strftime(timestamp_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);
}