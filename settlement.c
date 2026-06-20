#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <time.h>
#include <sys/stat.h>
#include "common.h"

int main() {
    mqd_t mq;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(TradeTicket);
    attr.mq_curmsgs = 0;
    
    mq = mq_open(IPC_MQ_NAME, O_CREAT | O_RDONLY, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(1);
    }
    
    printf("Settlement Process started. Waiting for trades via IPC...\n");
    
    mkdir("data", 0755);
    
    while(1) {
        TradeTicket ticket;
        ssize_t bytes_read = mq_receive(mq, (char*)&ticket, sizeof(TradeTicket), NULL);
        if (bytes_read >= 0) {
            int fd = open("data/ledger.txt", O_CREAT | O_WRONLY | O_APPEND, 0644);
            if (fd >= 0) {
                flock(fd, LOCK_EX);
                
                char buffer[256];
                struct tm *tm_info = localtime(&ticket.timestamp);
                char time_str[26];
                strftime(time_str, 26, "%Y-%m-%d %H:%M:%S", tm_info);
                
                int len = snprintf(buffer, sizeof(buffer), "[%s] Trade Match: %s bought %d shares from %s @ $%.2f\n", 
                                   time_str, ticket.buyer, ticket.qty, ticket.seller, ticket.price);
                
                if (write(fd, buffer, len) < 0) {
                    perror("write");
                }
                
                fsync(fd);
                
                flock(fd, LOCK_UN);
                close(fd);
                
                printf("Settled to ledger: %s", buffer);
            }
        }
    }
    
    mq_close(mq);
    return 0;
}
