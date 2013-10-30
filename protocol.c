#include "protocol.h"

int stuffBytes(unsigned int numChars)
{
	int i=4; //avança os quatro primeiros (cabeçalho da trama)
	for(;i<numChars+4;i++)
	{
		if(writeBuf[i]==0x7e)
		{
			memmove(writeBuf+i+1,writeBuf+i,numChars-(i-4)); //avança todos os caracteres uma posição
			writeBuf[i]=0x7d;
			writeBuf[++i]=0x5e;
			numChars++; //há mais um caracter a transmitir
		}
		else if(writeBuf[i]==0x7d)
		{
			memmove(writeBuf+i+1,writeBuf+i,numChars-(i-4)); //avança todos os caracteres uma posição
			writeBuf[i]=0x7d;
			writeBuf[++i]=0x5d;
			numChars++; //há mais um caracter a transmitir
		}
	}
	return numChars; //retorna novo número de caracteres a transmitir
}	 

/* (Re-)envia a informacao existente em writeBuf ate 3 vezes
   É chamada na funcao alarm() */
void sendFrame()
{
	if(conta++ <= MAX_RETRIES)
	{
		write(globalFD, writeBuf, writeBufLen);
		printf("\tSENDING PACKET\n");
		alarm(3);
	}
	else
	{
		printf("CONNECTION LOST... GIVING UP\n");
		exit(1);
	}
}

//Constroi e envia a trama de controlo com 'c' como campo de controlo
void sendControlFrame(int fd, int c)
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

//Trata de iniciar a conexao entre o transmitter e o receiver
int setConnection(char * porta, int user) //0 - writer, 1 - receiver
{
	struct termios newtio;
  
    int sum = 0, speed = 0, fd;

    fd = open(porta, O_RDWR | O_NOCTTY );
    if (fd <0) {perror(porta); return -1; }

    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
      perror("tcgetattr");
      return -1;
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 5 chars received */
	
    tcflush(fd, TCIOFLUSH);

    if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
      perror("tcsetattr");
	  return -1;
    }

    printf("New termios structure set\n");
	
	(void)signal(SIGALRM, sendFrame); //instala a rotina que envia um sigalrm quando houver time-out, chamando a funcao sendFrame
	
	if(user==0) //writer
	{
		writeBuf[0] = FLAG;
		writeBuf[1] = A;
		writeBuf[2] = C_SET;
		writeBuf[3] = writeBuf[1] ^ writeBuf[2];
		writeBuf[4] = FLAG;
		writeBufLen = 5;
		printf("WRITING SET\n");

		
		globalFD = fd;
		sendFrame();

		if (readFrame(fd, NULL, NULL) != C_UA)
		{
			printf("\tWRONG SET RESPONSE, QUITING\n");
			return -1;
		}
		else
			printf("CONNECTION ESTABLISHED!\n");
	}
	else //reader
	{
		printf("AWAITING CONNECTION\n");
		if(readFrame(fd, NULL, NULL) != C_SET)
		{
			printf("\tSET FRAME MALFORMED, QUITING\n");
			return -1;
		}
		else
		{
			sendControlFrame(fd, C_UA);
			printf("CONNECTION ESTABLISHED\n");
		}
	}
	return fd;
}

int readData(int fd, char * buffer)
{
	int res, readC = readFrame(fd, buffer, &res);
	if(readC == C_DISC) //disconnect recebido
	{
		return 0;
	}
	else if(readC == -1) //invalid bcc2
	{
		printf("\nINVALID DATA, SENDING REJECT\n");
		sendControlFrame(fd, C_REJ ^ c);
		return -1;
	}
	else
	{
		sendControlFrame(fd, C_RR ^ c);
		return res;
	}
}

int readFrame(int fd, char * buffer, int *res)
{
	int i = 0, bcc2 = 0, state = 0;
    unsigned char readByte, readC;

    while(1)
    {
		read(fd, readBuf, 1);
		readByte = readBuf[0];
		switch(state)
		{
		case 0: //FLAG
			if(readByte == FLAG)
				state=1;
			break;
		case 1: //A
			if(readByte == A)
				state=2;
			else if(readByte == READER_A)
				state=5;
			else if(readByte != FLAG)
				state = 0;
			break;
		case 2: //C
			readC = readByte;
			if(readByte == FLAG)
				state = 1;
			else
				state = 3;
			break;
		case 3: // BCC1
			if(readByte == A ^ readC)
				state = 4;
			else if(readByte == FLAG)
							state = 1;
					else
							state = 0;
			break;
		case 4: //FLAG
			if(readByte == FLAG)
			{
				//reinicia timeouts
				alarm(0);
				conta=0;
				if ((readC == 0 || readC == 1) && readC == c) //DATA FRAME, NOT DUPLICATE
                {
					*res = i-1; //-1 é o bcc
					if(buffer[i-1] == bcc2)
					{
						c = !c;
					}
					else
					{
						return -1;
					}
                }
				return readC;
			}
			else
			{
				//printf("\t%x - %c\n", readByte, readByte);
				//printf("I: %i\n", i);
				
				//destuffing
				if(readByte==0x7d) //se caracter for 0x7d, destuff
				{
					read(fd, readBuf, 1); //lê o próximo
					readByte=readBuf[0];
					if(readByte == 0x5e)
						readByte = 0x7e;
					else if(readByte == 0x5d)
						readByte = 0x7d;
				}
				
				buffer[i] = readByte; //construir chunk (para escrita no ficheiro)
				if(i>0)
					bcc2^=buffer[i-1]; //para não fazer xor do bcc2 com o próprio bcc2 (seria sempre 0)
				i++;
			}
			break;
		case 5: //DISC COMMAND FROM READER
			readC = readByte;
			if(readByte == C_DISC)
				state = 3;
			else
				state = 0;
		}
    }
}

//Trata de terminar a conexao 
int disconnect(int fd, int user) //0 writer, 1 reader
{
	writeBuf[0]=FLAG;
	writeBuf[1]=A;
	if(user==1) //needs to change address
		writeBuf[1]=READER_A;
	writeBuf[2]=C_DISC;
	writeBuf[3]=writeBuf[1]^writeBuf[2];
	writeBuf[4]=FLAG;
	writeBufLen=5;
	printf("DISCONNECTING\n");
	
    globalFD = fd;
	sendFrame(); //Envia trama de controlo com C_DISC
	if(user==0)
	{
		int readC = readFrame(fd, NULL, NULL);
		if(readC != C_DISC)
			disconnect(fd, user); //wrong response, send disc again
		else
			sendControlFrame(fd, C_UA);
	}
	else
	{
		if(readFrame(fd, NULL, NULL) != C_UA) //se não receber UA
		{
			printf("INVALID DISCONNECT RESPONSE\n");
			return -1;
		}
	}
	
	sleep(1); //wait before closing connection
	
	//close connection
	if ( tcsetattr(fd,TCSANOW,&oldtio) == -1) {
      perror("tcsetattr");
      return -1;
    }

    close(fd);
	return 0;
}

//Funcao auxiliar que constroi e envia um trama de dados com um pacote 'packet'
int sendData(int fd, char *packet, int packetChars)
{
	//cabeçalho da trama
	writeBuf[0] = FLAG;
	writeBuf[1] = A;
	writeBuf[2] = c;
	writeBuf[3] = writeBuf[1] ^ writeBuf[2];
	int i, bcc2 = 0;
	
	//dados
	for (i = 4; i<packetChars + 4; ++i)
	{
		writeBuf[i] = packet[i - 4];
		//printf("writeBuf[%i] -> %x\n", i, writeBuf[i]);
		bcc2 ^= writeBuf[i];
	}
	writeBuf[i++] = bcc2;
	int packetCharsAfterStuffing = stuffBytes(packetChars+1); //Número de caracteres para fazer stuffing
	//printf("%x\n", bcc2);
	i=packetCharsAfterStuffing+4; //posição no índice seguinte é igual ao novo número de caracteres a transmitir + cabeçalho da trama
	writeBuf[i++] = FLAG;
	writeBufLen = i;

	printf("WRITING DATA FRAME\n");
	globalFD = fd;
	sendFrame();

	if (readFrame(fd, NULL, NULL) == C_RR ^ !c)  //RR
	{
		c = !c; //ALTERNA C
		printf("\tRECEIVER READY!\n");
	}
	else //REJECT OR INVALID RESPONSE
	{
		printf("\tREJECT! RESENDING!!!\n");
		sendData(fd, packet, packetChars); //reenvia
	}
	
	return packetChars;
}

