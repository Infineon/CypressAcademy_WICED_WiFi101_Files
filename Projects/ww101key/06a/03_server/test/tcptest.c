#include <inttypes.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 27708

typedef struct  {
  uint8_t *data;
  int len;
} msg_t;

msg_t message;

void illegalMessage(int maxMessage)
{

  message.len  = rand() % maxMessage;
  message.data = malloc(message.len);


  for(int i=0;i<message.len;i++)
    {
      message.data[i] = rand()%255;
    }

}

void legalRandomWrite()
{
  int address;
  int value;
  int reg;

  reg = rand() % 256;
  address = rand() % 0x10000;
  value = rand() % 0x10000;

  message.len = 11;
  message.data = malloc(11);

  sprintf((char *)message.data,"W%04X%02X%04x",address,reg,value);
  
  
}
  
int main(int argc, char const *argv[])
{
  struct sockaddr_in address;
  int sock = 0, valread;
  struct sockaddr_in serv_addr;

  long            ms; // Milliseconds
  struct timespec spec;

  clock_gettime(CLOCK_REALTIME, &spec);
  ms = round(spec.tv_nsec / 1.0e6);
  srand(ms);

  if(argc == 2)
    {
      switch(argv[1][0])
	{
	case 'R':
	  illegalMessage(20);
	  break;
	case 'L':
	  legalRandomWrite();
	  break;
	default:
	  printf("illegal option\n");
	  exit(0);
	  break;
	}
    }
  else
    {
      printf("tcptest R|L\n");
      printf("R = Random illegal\n");
      printf("L = Random legal\n");
      exit(0);
    }

  

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      printf("\n Socket creation error \n");
      return -1;
    }
  
  memset(&serv_addr, '0', sizeof(serv_addr));
  
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
      
  // Convert IPv4 and IPv6 addresses from text to binary form
  if(inet_pton(AF_INET, "198.51.100.3", &serv_addr.sin_addr)<=0) 
    {
      printf("\nInvalid address/ Address not supported \n");
      return -1;
    }
  
  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
      printf("\nConnection Failed \n");
      return -1;
    }

  send(sock , message.data , message.len , 0 );


  char c;
  while(read( sock , &c, 1))
  {
    putchar(c);
  }
  printf("\n");


  return 0;
}

