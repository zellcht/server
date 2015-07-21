#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/poll.h>
#include <signal.h>
#include "utils.h"

#define MAX_HEADER     20480
#define MAX_LINE        2560
#define MAX_COMMAND    10240
#define MAX_FILE_NAME   2560
#define MAX_FILE_LEN   81920
#define GET 0
#define POST 1
#define BSIZ   8192

#define	MAX_REQ 999
#define BUSY 1
#define FREE 0
#define INI -1

pthread_cond_t ct;

char buffer[BSIZ];

char *methods[2] = {"GET", "POST"};

struct reqarg {
  int client;
  char reqline[MAX_LINE];
  char reqhead[MAX_HEADER];
  int reqid;
  pthread_mutex_t *reqm;
};

typedef struct reqarg reqarg_t;

typedef struct cons{
  int socket;
  int status;
} cons;

typedef struct htpt{
  char *host;
  int port;
  cons *connect;
} htpt;


htpt *hostport;
int maxcons;

int getfreecons(htpt *hostport, char *hostname, int portno);
int getconsnum( htpt *hostport, char *hostname, int portno);
int newcons( htpt *hostport, char *hostname, int portno);
int setfree( htpt *hostport, char *hostname, int portno, int server);


int getfreecons(htpt *hostport, char *hostname, int portno) {
  int server, num, socket, status, i,j;
  char *name;
  status = 0;
  server = INI;
  for(i = 0; i < MAX_REQ; i++) {
    name = hostport[i].host;
    num = hostport[i].port;
    if((name != NULL) && (name == hostname) && (num == portno)) {
      for(j = 0; j < maxcons; j++) {
	socket =  hostport[i].connect[j].socket;
	status = hostport[i].connect[j].status;
	if(socket > 0 && status == FREE) {
	  server = hostport[i].connect[j].socket ;
	  hostport[i].connect[j].status = BUSY;
	  status = BUSY;
	  break;
	}
      }
      if(status == BUSY) {
	break;
      }
    }
  }
  return server;
}

int getconsnum(htpt *hostport, char *hostname, int portno) {
  int num, connection_num, socket, i,j;
  char *name;
  connection_num = 0;
  for(i = 0; i < MAX_REQ; i++) {
    name = hostport[i].host;
    num = hostport[i].port;
    if((name != NULL) && (name == hostname) && (num == portno)) {
      for(j = 0; j < maxcons; j++) {
	socket =  hostport[i].connect[j].socket;
	if(socket > 0){
	  connection_num++;
	}
      }
      break;
    }
  }
  return connection_num;
}

int newcons( htpt *hostport, char *hostname, int portno){
  int server, num, socket, status, i,j;
  char *name;
  status = 0;
  server = INI;

  for( i = 0; i < MAX_REQ; i++) {
    name = hostport[i].host;
    num = hostport[i].port;
    if((name != NULL) && (name == hostname) && (num == portno)) {
      for( j = 0; j < maxcons; j++) {
	socket =  hostport[i].connect[j].socket;
	status = hostport[i].connect[j].status;
	if(socket == 0 && status == FREE) {
	  status = BUSY;
	  server = activesocket(hostname, portno);
	  hostport[i].connect[j].socket = server;
	  hostport[i].connect[j].status = BUSY;
	  break;
	}
      }
      if(status == BUSY) {
	break;
      }
    }
  }
  if(status == FREE) {
    for(i = 0; i < MAX_REQ; i++) {
      name = hostport[i].host;
      num = hostport[i].port;
      if((name == NULL) && (num == 0)){
	hostport[i].host = hostname;
	hostport[i].port = portno;
	server = activesocket(hostname, portno);
	hostport[i].connect[0].socket = server;
	break;
      }
    }
  }

  return server;
}


int setfree( htpt *hostport, char *hostname, int portno, int server) {
  int num, socket, i, j;
  char *name;
  for( i = 0; i < MAX_REQ; i++) {
    name = hostport[i].host;
    num = hostport[i].port;
    if((name != NULL) && (name == hostname) && (num == portno)) {
      for( j = 0; j < maxcons; j++) {
	socket =  hostport[i].connect[j].socket;
	if(socket == server){
	  hostport[i].connect[j].status = FREE;
	  break;
	}
      }
    }
  }
  return 0;
}

int contentlength(char *header)
{
  int len = INT_MAX;
  char line[MAX_LINE];

  if (HTTPheadervalue_case(header, "Content-Length", line)) {
    sscanf(line, "%d", &len);
  }
  return len;
}

void parserequest(char *request, int *method, char *host, int *portno, char *path, char *prot)
{
  char url[MAX_FILE_NAME] = "";
  char m[MAX_FILE_NAME] = "";
  char hostport[MAX_FILE_NAME] = "";
  char port[MAX_FILE_NAME] = "";

  sscanf(request, "%[^ ] %[^ ] HTTP/%[^ \r\n]", m, url, prot);
  if (strcmp(m, "GET") == 0) {
    *method = GET;
  }
  else if (strcmp(m, "POST") == 0) {
    *method = POST;
  }
  sscanf(url, "http://%[^\n\r/]%[^\n\r]", hostport, path);
  sscanf(hostport, "%[^:]:%[^\n\r]", host, port);
  if (*port == '\0') {
    *portno = 80;
  }
  else {
    *portno = atoi(port);
  }
}

int parseresponse(char *statusline)
{
  int i;

  sscanf(statusline, "HTTP/1.1 %d ", &i);
  return i;
}

void *request(void *a)
{
  char reqhead1[MAX_HEADER];
  char reshead[MAX_HEADER];
  char resline[MAX_LINE];
  char host[MAX_FILE_NAME];
  char path[MAX_FILE_NAME];
  char prot[MAX_FILE_NAME];
  char tbuff[BSIZ];
  int server, i, portno, method, status;
  int n, rlen, tlen, clen;
  unsigned int chlen;
  reqarg_t *arg = (reqarg_t *)a;
  int client = arg->client;
  char *reqline = arg->reqline;
  char *reqhead = arg->reqhead;
  int reqnum = arg->reqid;
  pthread_mutex_t *reqm = arg->reqm;
  char reqid[10];
  int numconn;
  
  parserequest(reqline, &method, host, &portno, path, prot);

  sprintf(reqhead1, "%s %s HTTP/1.1\r\n", methods[method], path);
  HTTPheaderremove_case(reqhead, "connection");
 
  strcat(reqhead1, "Connection: keep-alive\r\n");
  strcat(reqhead1, reqhead);

  pthread_mutex_lock(reqm);
  while(1){
    server = getfreecons(hostport, host, portno);
    while (!(server > 0)) {
      if(server == INI) {
	numconn = getconsnum(hostport, host, portno);
	if(numconn < maxcons) {
	  server = newcons(hostport, host, portno);
	} 
	else{
	  while(server == INI) {
	    pthread_cond_wait(&ct, reqm);
	  }
	}
      }
    }

    n = strlen(reqhead1);
    if (write(server, reqhead1, n) < n) {
      close(server);
      continue;
    }
    if (method == POST) {
      rlen = contentlength(reqhead);
      i = 0;
      n = 0;
      while (rlen > 0) {
	tlen = BSIZ;
	if (rlen < tlen) {
	  tlen = rlen;
	}
	n = read(client, tbuff, tlen);
	if (n <= 0) break;
	i = write(server, tbuff, n);
	if (i < n) break;
	rlen -= n;
      }
      if (n <= 0 || i < n) {
	close(server);
	continue;
      }
    }
    if (TCPreadline(server, resline, MAX_LINE) == 0) {
      close(server);
      continue;
    }
    if (HTTPreadheader(server, reshead, MAX_HEADER) == 0) {
      close(server);
      continue;
    }
    break;
  }


  sprintf(reqid, "%x\r\n", reqnum);
  write( client, reqid, strlen(reqid));

  write(client, resline, strlen(resline));
	
  HTTPheaderremove_case(reshead, "connection");
  write(client, reshead, strlen(reshead));

  if (HTTPheadervalue_case(reshead, "Transfer-Encoding", resline) && strcasecmp(resline, "chunked") == 0) {
    while (1) {
      TCPreadline(server, resline, MAX_LINE);
      write( client, reqid, strlen(reqid)); 
      write( client, resline, strlen(resline));
      sscanf(resline, "%x", &chlen);
      if (chlen == 0) break;
      while (chlen > 0) {
	tlen = BSIZ;
	if (chlen < tlen) {
	  tlen = chlen;
	}
	n = read(server, tbuff, tlen);
	chlen -= n;
	i = write(client, tbuff, n);
      }
      read(server, tbuff, 1);
      if (*tbuff != '\r') printf("Error %d\n", *tbuff);
      read(server, tbuff, 1);
      if (*tbuff != '\n') printf("Error %d\n", *tbuff);
      write(client, "\r\n", 2);
    }
    read(server, tbuff, 1);
    if (*tbuff != '\r') printf("Error %d\n", *tbuff);
    read(server, tbuff, 1);
    if (*tbuff != '\n') printf("Error %d\n", *tbuff);
    write(client, "\r\n", 2);
  } else 
    {
    status = parseresponse(resline);
    if (status != 204 && status != 304) {
      clen = contentlength(reshead);
      while (clen > 0) {
	tlen = BSIZ;
	if (clen < tlen) {
	  tlen = clen;
	}
	n = read(server, tbuff, tlen);
	if (n <= 0) break;
	i = write(client, tbuff, n);
	if (i < n) break;
	clen -= n;
      } if (n <= 0 || i < n) {
	close(server);
      }
    }
  }
  setfree(hostport, host, portno,server);
  pthread_mutex_unlock(reqm);

  return NULL;
}

void *connection(void *a)
{
  char temp[MAX_FILE_NAME];
  reqarg_t *arg;
  int client = *(int *)a;
  int reqid_c;
  reqid_c = 0;

  pthread_mutex_t reqm;
  pthread_mutex_init( &reqm, NULL);
  do {
    arg = (reqarg_t *) malloc(sizeof(reqarg_t));
    arg->client = client;

    arg->reqid = reqid_c;
    reqid_c++;
    //printf("reqid-c:%d\n", arg->reqid);
    if (TCPreadline(client, arg->reqline, MAX_LINE) == 0) {
      break;
    }
    //printf("reqline-c:%s\n", arg->reqline);
    HTTPreadheader(client, arg->reqhead, MAX_HEADER);
    //printf("reqhead-c:%s\n", arg->reqhead);

    arg->reqm = &reqm;
    request(arg);
  } while (!HTTPheadervalue_case(arg->reqhead, "connection", temp) || strcasecmp(temp, "close") != 0);
  free(arg);
  

  return NULL;
}

void inits(){
  int i;
  pthread_cond_init( &ct, NULL);
  hostport = (htpt*)malloc((MAX_REQ)*sizeof(htpt)); 
  for (i = 0; i < MAX_REQ; i++){
    hostport[i].connect = (cons*)malloc(sizeof(cons)*(maxcons));
  }

}

int main(int argc, char *argv[])
{
  int port, serversocket;
  int *arg;
  pthread_t t;
  int max;

  sigignore(SIGPIPE);
  port = atoi(argv[1]);
  max = atoi(argv[3]);
  maxcons = max;

  printf("**port %d\n", port);
  if ((serversocket = passivesocket(port)) < 0) {
    perror("open");
    exit(1);
  }

  if(max > 10 || max <= 0 ){
    printf("The max number of connections is 10.\n");
    exit(1);
  }

  inits();

  while(1) {
    arg = (int *)malloc(sizeof(int));
    *arg = acceptconnection(serversocket);
    pthread_create(&t, NULL, connection, arg);
  }
  free(hostport);
  return 0;
} 
