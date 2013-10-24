/*Non-Canonical Input Processing*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <math.h>

#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FLAG 0x7E
#define A 0x03
#define C_SET 0x03
#define C_UA 0x07
#define C_RR 0x05
#define C_REJ 0x01
#define C_DISC 0x0B
#define FALSE 0
#define TRUE 1

#define MAX_RETRIES 3 /* N�mero de envios adicionais a efectuar em caso de n�o haver resposta */
#define CHUNK_SIZE 1024


int fd, conta = 0, writeBufLen, res, c=0, fileSize;
char writeBuf[CHUNK_SIZE+10], chunkBuf[CHUNK_SIZE], readBuf[255], *fileContent;

int readResponse()
{
	res = read(fd,readBuf,1); //parado aqui
    if(readBuf[0]==FLAG) //FLAG
	{
		res=read(fd,readBuf,1);
		if(readBuf[0]==A) //ADDRESS
		{
			res=read(fd,readBuf,1); //C
			int readC = readBuf[0];
			res=read(fd,readBuf,1);
			if(readBuf[0]==A^readC) //BCC1
			{
				res=read(fd,readBuf,1);
				if(readBuf[0]==FLAG) //FLAG
				{
					alarm(0); //cancelar alarmes pendentes (ATENÇÂO A RESPOSTAS INVÁLIDAS, NÃO ESPERADAS)
					conta = 0; //reinicia contador de retries
					return readC;
				}
			}
		}
	}
	return -1; //ERROR
}

void sendTrama()
{
	if(conta++ <= MAX_RETRIES)
	{
		write(fd, writeBuf, writeBufLen);
		printf("\tSENDING PACKET\n");
		alarm(3);
	}
	else
	{
		printf("CONNECTION LOST... GIVING UP\n");
		exit(1);
	}
}


void sendControlTrama(int c)
{
	writeBuf[0]=FLAG;
    writeBuf[1]=A;
    writeBuf[2]=c;
    writeBuf[3]=writeBuf[1]^writeBuf[2];
    writeBuf[4]=FLAG;
	writeBufLen=5;
	printf("SENDING COMMAND 0x%x\n", c);
	write(fd, writeBuf, writeBufLen);  
}

void disconnect()
{
	writeBuf[0]=FLAG;
	writeBuf[1]=A;
	writeBuf[2]=C_DISC;
	writeBuf[3]=writeBuf[1]^writeBuf[2];
	writeBuf[4]=FLAG;
	writeBufLen=5;
	printf("DISCONNECTING\n");
	
    (void) signal(SIGALRM, sendTrama);
    sendTrama();
	int readC = readResponse();
	if(readC != C_DISC)
		disconnect(); //wrong response, send disc again
	else
		sendControlTrama(C_UA);
}

void setConnection()
{
	writeBuf[0]=FLAG;
	writeBuf[1]=A;
	writeBuf[2]=C_SET;
	writeBuf[3]=writeBuf[1]^writeBuf[2];
	writeBuf[4]=FLAG;
	writeBufLen=5;
	printf("WRITING SET\n");
	
    (void) signal(SIGALRM, sendTrama);
    sendTrama();
	if(readResponse() != C_UA)
	{
		printf("\tWRONG SET RESPONSE, QUITING\n");
		exit(1);
	}
	else
		printf("CONNECTION ESTABLISHED!\n");
}

void sendDataTrama(char *trama, int numchars)
{
	//Trama de teste
	writeBuf[0]=FLAG;
	writeBuf[1]=A;
	writeBuf[2]=c;
	writeBuf[3]=writeBuf[1]^writeBuf[2];
	int i;
	printf("ENTROU\n");
	for(i=4;i<numchars+4;++i) //APPEND
	{
		//printf("chunkBuf[%i] -> %c\n", i-4, trama[i-4]);
		writeBuf[i] = trama[i-4];
	}
	writeBuf[i++]=0; //XOR COM TODOS OS BYTES ENVIADOS
	writeBuf[i++]=FLAG;
	writeBufLen = i;
	
	printf("WRITING TRAMA\n");
	sendTrama();
	
	if(readResponse() != C_RR ^ !c) //aguarda RR
	{
		printf("\tREJECT! RESENDING!!!\n");
		sendDataTrama(trama, numchars); //reenvia
	}
	else
	{
		c = !c;
		printf("\tRECEIVER READY!\n");
	}
}

int openFile(char *fileName)
{
	FILE * pFile;
	long lSize;
	size_t result;

	pFile = fopen ( fileName , "rb" );
	if (pFile==NULL) {fputs ("File error",stderr); exit (1);}

	// obtain file size:
	fseek (pFile , 0 , SEEK_END);
	lSize = ftell (pFile);
	rewind (pFile);

	// allocate memory to contain the whole file:
	fileContent = (char*) malloc (sizeof(char)*lSize);
	if (fileContent == NULL) {fputs ("Memory error",stderr); exit (2);}

	// copy the file into the buffer:
	result = fread (fileContent,1,lSize,pFile);
	if (result != lSize) {fputs ("Reading error",stderr); exit (3);}

	/* the whole file is now loaded in the memory buffer. */

	// terminate
	fclose (pFile);
	return lSize;
}

int main(int argc, char** argv)
{
    struct termios oldtio,newtio;
  
    int i, sum = 0, speed = 0;
    
    if (argc < 3){
      printf("Usage:\tnserial SerialPort FileName\n\tex: nserial /dev/ttyS1 text.txt\n");
      exit(1);
    }

  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */

    fd = open(argv[1], O_RDWR | O_NOCTTY );
    if (fd <0) {perror(argv[1]); exit(-1); }

    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 5 chars received */



  /* 
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a 
    leitura do(s) pr�ximo(s) caracter(es)
  */


    tcflush(fd, TCIOFLUSH);

    if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    printf("New termios structure set\n");   

	fileSize = openFile(argv[2]);
	
	printf("FILESIZE: %i\n", fileSize);
	
   /* printf("WRITING PACKET\n");
    res = write(fd,buf,5);
    printf("%d bytes written\n", res);*/
    
	(void) signal(SIGALRM, sendTrama);
	setConnection();
	
	for(i=0;i<(fileSize/CHUNK_SIZE);i++) //whole chunks
	{
		memcpy(chunkBuf, &fileContent[i*CHUNK_SIZE], CHUNK_SIZE);
		sendDataTrama(chunkBuf, CHUNK_SIZE);
	}
	
	//last bytes
	if(fileSize%CHUNK_SIZE>0)
	{
		memcpy(chunkBuf, &fileContent[i*CHUNK_SIZE], fileSize%CHUNK_SIZE);
		sendDataTrama(chunkBuf, fileSize%CHUNK_SIZE);
	}
	
	disconnect();
	
	printf("\nALL DONE\n");
	sleep(1);
    
    if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    close(fd);
    return 0;
}

