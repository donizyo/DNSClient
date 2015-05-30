
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <signal.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#define BUFFER_SIZE 65536
#define MSZ_NS      16

static char dns_servers[16][MSZ_NS];
static unsigned char * buf;


#define T_A 1
#define T_NS 2
#define T_CNAME 5
#define T_SOA 6
#define T_PTR 12
#define T_MX 15

void resolveHostname(unsigned char*, int);
void encodeHostname(unsigned char*, unsigned char*);
unsigned char* decodeHostname(unsigned char*, unsigned char*, int*);
void
loadConf();

inline static int
send_dns_request(unsigned char *buf, int len, int protocol,
        int sock, struct sockaddr_in dest)
{
    int res;

    switch (protocol)
    {
        case IPPROTO_UDP:
            res = sendto(sock, (char*) buf, len, 0,
                    (struct sockaddr*) &dest,
                    sizeof (struct sockaddr_in));
            break;
            /*
        case IPPROTO_TCP:
            res = connect(sock,
                    (struct sockaddr *) &dest,
                    sizeof (struct sockaddr_in));
            if (res < 0)
                return res;
            res = htonl(len);
            res = send(sock, &res, sizeof(int), 0);
            if (res < 0)
                return res;
            res = send(sock, buf, len, 0);
            break;
            */
        default:
            return -1;
    }
    return res;
}

struct DNS_HEADER
{
    unsigned short id;

    unsigned char rd : 1;
    unsigned char tc : 1;
    unsigned char aa : 1;
    unsigned char opcode : 4;
    unsigned char qr : 1;

    unsigned char rcode : 4;
    unsigned char z : 3;
    unsigned char ra : 1;

    unsigned short qdcount;
    unsigned short ancount;
    unsigned short nscount;
    unsigned short arcount;
};

struct QUESTION
{
    unsigned short qtype;
    unsigned short qclass;
};

#pragma pack(push, 1)

struct R_DATA
{
    unsigned short type;
    unsigned short _class;
    unsigned int ttl;
    unsigned short data_len;
};
#pragma pack(pop)

struct RES_RECORD
{
    unsigned char *name;
    struct R_DATA *resource;
    unsigned char *rdata;
};

typedef struct
{
    unsigned char *name;
    struct QUESTION *ques;
} QUERY;

void
handle_Interruption(int param)
{
    printf("I'm dead thanks to [%i]!\r\n", param);
    if (buf != NULL)
    {
        free(buf);
        buf = (unsigned char *) NULL;
    }
    exit(SIGINT);
}

int
main(int argc, char *argv[])
{
    unsigned char hostname[256];

    if (buf != NULL)
    {
        printf("There are already an running instance!\r\n");
        return -1;
    }
    buf = (unsigned char *) NULL;
    signal(SIGINT, handle_Interruption);

    printf("Enter Hostname to Lookup : ");
    scanf("%s", hostname);
    loadConf();

    resolveHostname(hostname, T_A);

    return 0;
}

void
resolveHostname(unsigned char *host, int query_type)
{
    unsigned char *qname, *reader;
    int i, j, stop, sock;
    int len;
    struct sockaddr_in a;
    struct RES_RECORD answers[20], auth[20], addit[20];
    struct sockaddr_in dest;
    struct DNS_HEADER *dns = NULL;
    struct QUESTION *qinfo = NULL;
    int ancount, nscount, arcount;
    int type;
    int protocol;

    type = SOCK_DGRAM;
    protocol = IPPROTO_UDP;
    printf("Allocating memory...\r\n");
    buf = (unsigned char *) malloc(BUFFER_SIZE);
    if (buf == NULL)
        return;
    bzero(buf, BUFFER_SIZE);

    printf("Resolving %s\r\n", host);

    {
        dns = (struct DNS_HEADER *) buf;
        dns->id = htons((unsigned short) (0xffffu & getpid()));
        dns->rd = 1;
        dns->qdcount = htons(1);
        len = sizeof (struct DNS_HEADER);

        qname = (unsigned char*) (buf + len);
        encodeHostname(qname, host);
        len += (strlen((const char *) qname) + sizeof (unsigned char));
        qinfo = (struct QUESTION*) (buf + len);
        qinfo->qtype = htons(query_type);
        qinfo->qclass = htons(1);
        len += sizeof (struct QUESTION);
    }

    sock = socket(AF_INET, type, protocol);
    dest.sin_family = AF_INET;
    dest.sin_port = htons(53);
    dest.sin_addr.s_addr = inet_addr(dns_servers[0]);
    i = sizeof (struct sockaddr_in);

    printf("Sending Packet...");
    if (send_dns_request(buf, len, protocol, sock, dest) < 0)
    {
        perror("failed\r\n");
        free(buf);
        buf = NULL;
        return;
    }
    printf("done\r\n");

    printf("Receiving answer...");
    if (recvfrom(sock, (char*) buf, BUFFER_SIZE, 0, (struct sockaddr*) &dest, (socklen_t*) & i) < 0)
    {
        perror("failed\r\n");
        free(buf);
        buf = NULL;
        return;
    }
    printf("done\r\n");

    dns = (struct DNS_HEADER*) buf;
    ancount = ntohs(dns->ancount);
    nscount = ntohs(dns->nscount);
    arcount = ntohs(dns->arcount);

    reader = buf + len;

    printf("\nThe response contains : ");
    printf("\n %d Questions.", ntohs(dns->qdcount));
    printf("\n %d Answers.", ancount);
    printf("\n %d Authoritative Servers.", nscount);
    printf("\n %d Additional records.\n\n", arcount);

    stop = 0;

    for (i = 0; i < ancount; i++)
    {
        answers[i].name = decodeHostname(reader, buf, &stop);
        reader = reader + stop;

        answers[i].resource = (struct R_DATA*) (reader);
        reader = reader + sizeof (struct R_DATA);

        if (ntohs(answers[i].resource->type) == 1) //if its an ipv4 address
        {
            answers[i].rdata = (unsigned char*) malloc(ntohs(answers[i].resource->data_len));

            for (j = 0; j < ntohs(answers[i].resource->data_len); j++)
            {
                answers[i].rdata[j] = reader[j];
            }

            answers[i].rdata[ntohs(answers[i].resource->data_len)] = '\0';

            reader = reader + ntohs(answers[i].resource->data_len);
        }
        else
        {
            answers[i].rdata = decodeHostname(reader, buf, &stop);
            reader = reader + stop;
        }
    }

    for (i = 0; i < nscount; i++)
    {
        auth[i].name = decodeHostname(reader, buf, &stop);
        reader += stop;

        auth[i].resource = (struct R_DATA*) (reader);
        reader += sizeof (struct R_DATA);

        auth[i].rdata = decodeHostname(reader, buf, &stop);
        reader += stop;
    }

    for (i = 0; i < arcount; i++)
    {
        addit[i].name = decodeHostname(reader, buf, &stop);
        reader += stop;

        addit[i].resource = (struct R_DATA*) (reader);
        reader += sizeof (struct R_DATA);

        if (ntohs(addit[i].resource->type) == 1)
        {
            addit[i].rdata = (unsigned char*) malloc(ntohs(addit[i].resource->data_len));
            for (j = 0; j < ntohs(addit[i].resource->data_len); j++)
                addit[i].rdata[j] = reader[j];

            addit[i].rdata[ntohs(addit[i].resource->data_len)] = '\0';
            reader += ntohs(addit[i].resource->data_len);
        }
        else
        {
            addit[i].rdata = decodeHostname(reader, buf, &stop);
            reader += stop;
        }
    }

    printf("\nAnswer Records : %d \n", ancount);
    for (i = 0; i < ancount; i++)
    {
        printf("Name : %s ", answers[i].name);

        if (ntohs(answers[i].resource->type) == T_A)
        {
            long *p;
            p = (long*) answers[i].rdata;
            a.sin_addr.s_addr = (*p);
            printf("has IPv4 address : %s", inet_ntoa(a.sin_addr));
        }

        if (ntohs(answers[i].resource->type) == 5)
        {
            printf("has alias name : %s", answers[i].rdata);
        }

        free(answers[i].name);
        free(answers[i].rdata);
        printf("\n");
    }

    printf("\nAuthoritive Records : %d \n", nscount);
    for (i = 0; i < nscount; i++)
    {

        printf("Name : %s ", auth[i].name);
        if (ntohs(auth[i].resource->type) == 2)
        {
            printf("has nameserver : %s", auth[i].rdata);
        }
        free(auth[i].name);
        free(auth[i].rdata);
        printf("\n");
    }

    printf("\nAdditional Records : %d \n", arcount);
    for (i = 0; i < arcount; i++)
    {
        printf("Name : %s ", addit[i].name);
        if (ntohs(addit[i].resource->type) == 1)
        {
            long *p;
            p = (long*) addit[i].rdata;
            a.sin_addr.s_addr = (*p);
            printf("has IPv4 address : %s", inet_ntoa(a.sin_addr));
        }
        free(addit[i].name);
        free(addit[i].rdata);
        printf("\n");
    }

    printf("Releasing memory...\r\n");
    free(buf);
    buf = NULL;
}

unsigned char *
decodeHostname(unsigned char* reader, unsigned char* buffer, int* count)
{
    unsigned char *name;
    unsigned char flag;
    unsigned short offset;
    unsigned int i, j;

    i = 0;
    j = 0;
    *count = 1;
    name = (unsigned char*) malloc(256);
    if (name == NULL)
        return (unsigned char *) NULL;

    while (*reader != 0)
    {
        flag = *reader >> 6;
        //printf("Flag: %i\r\n", flag);
        if (flag == 3)
        {
            offset = (*reader)*256 + *(reader + 1) - 49152;
            //printf("    Offset: %i\r\n", offset);
            reader = buffer + offset;
            j = 1;
        }
        else if (flag == 0)
        {
            flag = *reader;
            //printf("    Len: %i\r\n", flag);
            memcpy(name + i, ++reader, flag);
            //printf("    String: %s\r\n", name + i);
            i += flag;
            name[i++] = '.';
            reader += flag;
            if (j == 0)
                *count += (1 + flag);
        }
        else
        {
            free(name);
            name = (unsigned char *) NULL;
            return name;
        }
    }

    if (j == 1)
        ++*count;

    name[i - 1] = '\0';

    return name;
}

void
loadConf()
{
    FILE *file;
    char *line, *ip, *save;
    int i, sz_line;

    i = 0;
    if (i < MSZ_NS)
        strcpy(dns_servers[i++], "8.8.8.8\0");
    if (i < MSZ_NS)
        strcpy(dns_servers[i++], "208.67.222.222\0");
    if (i < MSZ_NS)
        strcpy(dns_servers[i++], "208.67.220.220\0");

    if ((file = fopen("/etc/resolv.conf", "r")) == NULL)
        return;

    sz_line = 128;
    line = (char *) malloc(sz_line);
    if (line == NULL)
    {
        fclose(file);
        return;
    }

    while (bzero(line, sz_line),
            fgets(line, sz_line, file) != NULL && i < MSZ_NS)
    {
        if (line[0] == '#')
            continue;
        if (strncmp(line, "nameserver", 10) == 0)
        {
            strtok_r(line, " ", &save);
            ip = strtok_r(NULL, " ", &save);
            strcpy(dns_servers[i++], ip);
        }
    }

    free(line);
    line = (char *) NULL;
    fclose(file);
}

void
encodeHostname(unsigned char* dns, unsigned char* host)
{
    int lock = 0, i;
    strcat((char*) host, ".");

    for (i = 0; i < strlen((char*) host); i++)
    {
        if (host[i] == '.')
        {
            *dns++ = i - lock;
            for (; lock < i; lock++)
            {
                *dns++ = host[lock];
            }
            lock++;
        }
    }
    *dns++ = '\0';
}