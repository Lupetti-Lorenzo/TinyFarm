#include <stdlib.h>

void sendLong(int fd, long n);
void sendString(int fd, char *s);
void *tbodyc(void *arg);
void *sigintHandler(void *v);
ssize_t readn(int fd, void *ptr, size_t n);
ssize_t writen(int fd, void *ptr, size_t n);
void print_usage(char *nome_exec);