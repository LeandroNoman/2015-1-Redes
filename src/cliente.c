#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tp_socket.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define BOOL int
#define TRUE 1
#define FALSE 0
#define FILEANSWERSIZE 5

//Verifica se os argumentos passados estao corretos
BOOL verificarArgumentos(int argc, char* argv[]);
//Envia o tamanho da janela para o servidor
BOOL iniciaConexao(int sock, so_addr *soaddr, int bufflen, int windowlen);
//Envia o nome do arquivo ao servidor
BOOL enviaString(int sock, so_addr *servidor, char *string, char *buff, int mtu);
//Recebe o conteudo do arquivo do servidor e o escreve no disco
unsigned int recebeArquivo(FILE *file, int sock, char *buff, int bufflen, int mtu, int windowlen, so_addr *soaddr);
//Checa o se uma mensagem foi recebida sem erro
BOOL checkCheckSum(char *buff, int bufflen);
//Cria o campo de checksum em uma mensagem a ser enviada
void createCheckSum(char *buff, int bufflen);
//Imprime o tempo de execucao no fim do programa
void imprimeDiferencaTempo(struct timeval *tv1, struct timeval *tv2, unsigned int bytesRecebidos, int bufflen);

int main(int argc, char* argv[]) {
	so_addr soaddr;
	int status, sock, bufflen, windowlen, i, mtu;
	unsigned int bytesRecebidos;
	char *buff;
	FILE *outputFile;
	struct timeval tempoInicial, tempoFinal;

	//Verifica se os argumentos sao validos
	if(!verificarArgumentos(argc, argv)) {
		exit(1);
	}
	
	//Cria o buffer
	windowlen = atoi(argv[5]);

	bufflen = atoi(argv[4]);
	buff = NULL;
	buff = malloc(bufflen);
	if(buff == NULL) {
		printf("Erro ao criar o buffer!\n");
		exit(2);
	}

	//Inicializa a biblioteca de sockets
	status = tp_init();
	if(status < 0) {
		printf("Erro ao inicializar a biblioteca de sockets!\n");
		exit(3);
	}

	//Verifica o tamanho maximo dos pacotes enviados pela rede
	mtu = tp_mtu();

	//Verifica o tamanho do buffer em relacao ao mtu
	if(bufflen < mtu) {
		printf("O tamanho do buffer deve ser no minimo igual ao tamanho do mtu!\n");
		exit(4);
	}
	
	//Cria o socket
	sock=socket(PF_INET,SOCK_DGRAM,0);
	if(sock < 0) {
		printf("Erro ao criar o socket com o servidor!\n");
		exit(5);
	}
	
	status = tp_build_addr(&soaddr, argv[1], atoi(argv[2]));
	if(status != 0) {
		printf("Erro ao criar o socket com o servidor!\n");
		exit(6);
	}

	//Primeira chamada para contabilizar o tempo gasto
	gettimeofday(&tempoInicial, NULL);

	//Envia o tamanho da janela para o servidor
	status = iniciaConexao(sock, &soaddr, bufflen, windowlen);
	if(!status) {
		printf("Erro ao enviar o tamnho da janela ao servidor!\n");
		exit(7);
	}
	
	//Envia o nome do arquivo para o servidor
	status = enviaString(sock, &soaddr, argv[3], buff, mtu);
	if(!status) {
		printf("Erro ao enviar o nome do arquivo ao servidor!\n");
		exit(8);
	}

	//Abre o arquivo de escrita
	outputFile = fopen(argv[3], "w+");
	if(outputFile == NULL) {
		printf("Nao foi possivel abrir o arquivo para escrita.\n");
		exit(9);
	}

	//Recebe o conteudo do arquivo
	bytesRecebidos = recebeArquivo(outputFile, sock, buff, bufflen, mtu, windowlen, &soaddr);

	//Chamada final para contabilizar o tempo gasto
	gettimeofday(&tempoFinal, NULL);

	//Imprime a diferenca de tempo no fim do programa
	imprimeDiferencaTempo(&tempoInicial, &tempoFinal, bytesRecebidos, bufflen);

	//Fecha o arquivo de escrita;
	fclose(outputFile);
	
	return 0;
}

/**Verifica se os argumentos sao validos.
 * Retorna TRUE se sim e FALSE caso contrario.
 */
BOOL verificarArgumentos(int argc, char* argv[]) {
	if(argc != 6) {
		printf("Argumentos necessarios: 'host do servidor' 'porto do servidor' 'nome do arquivo' 'tamanho do buffer' 'tamanho da janela'.\n");
		return FALSE;
	}

	return TRUE;
}

//Envia o tamanho maximo da janela para o servidor
BOOL iniciaConexao(int sock, so_addr *soaddr, int bufflen, int windowlen) {
	int returnv, i, flagRecebido, checksum;
	char envio[6], resposta;

	//Os primeiros 4 bytes da mensagem indicam o tamanho da janela do cliente
	envio[0] = (windowlen >> 24) & 0xFF;
	envio[1] = (windowlen >> 16) & 0xFF;
	envio[2] = (windowlen >> 8) & 0xFF;
	envio[3] = windowlen & 0xFF;

	//Coloca o checksum na mensagem
	createCheckSum(envio, 4);

	flagRecebido = 1;
	//Enquando a mensagem nao for recebida corretamente, continua a reenvia-la
	while(flagRecebido) {
		//Envia a mensagem
		returnv = tp_sendto(sock, envio, sizeof(envio), soaddr);
		//Recebe a confirmacao
		tp_recvfrom(sock, &resposta, sizeof(resposta), soaddr);
		//Se a confirmacao for positiva, finaliza
		if(resposta == 0) {
			flagRecebido = 0;
			break;
		}
		//Se o servidor nao recebeu a mensagem correta, reenvia
		else {
			flagRecebido = 1;
		}
	}

	if(returnv < 0) {
		return FALSE;
	}
	return TRUE;
}

//Envia uma string para o servidor
BOOL enviaString(int sock, so_addr *servidor, char *string, char *buff, int mtu) {
	int returnv, i, flagResend, pointer, bytesleft;
	char resposta;

	flagResend = 0;
	pointer = 0;
	//Variavel para contabilizar quandos bytes ainda precisam ser enviados
	bytesleft = strlen(string) + 1;
	//Loop para enviar as mensagens para o servidor
	while(flagResend || bytesleft > 0) {
		//Se nao for para reenviar uma mensagem, cria uma nova com os dados restantes
		if(!flagResend) {
			for(i = 2; i < mtu - 2 && bytesleft != 0; i++) {
				buff[i] = string[pointer];
				pointer++;
				bytesleft--;
			}
			//Os dois primeiros bytes indicam quantos bytes ainda precisam ser enviados
			buff[0] = (bytesleft >> 8) & 0xFF;
			buff[1] = bytesleft & 0xFF;
			//Cria o checksum
			createCheckSum(buff, i);
		}
		//Envia a mensagem ao servidor
		returnv = tp_sendto(sock, buff, i + 2, servidor);
		//Recebe a confirmacao
		tp_recvfrom(sock, &resposta, sizeof(resposta), servidor);
		if(resposta == 0) {
			flagResend = 0;
		}
		else {
			flagResend = 1;
		}
	}
	
	if(returnv < 0) {
		return FALSE;
	}
	return TRUE;
}

//Envia a confirmacao de que um pacote chegou corretamente
void enviaConfirmacao(char *buff, int msgBegin, int flagMsgCorreta, int sock, so_addr *soaddr) {
	char msg[FILEANSWERSIZE];
	//Os primeiros dois bytes indicam o numero do pacote a ser confirmado, justamente como no pacote recebido
	msg[0] = buff[msgBegin];
	msg[1] = buff[msgBegin + 1];
	//Se a mensagem foi recebida corretamente, envia 0, se nao, envia -1
	if(flagMsgCorreta == 0) {
		msg[2] = -1;
	}
	else {
		msg[2] = 0;
	}
	//Cria o checksum da mensagem e envia ao servidor
	createCheckSum(msg, FILEANSWERSIZE-2);
	tp_sendto(sock, msg, FILEANSWERSIZE, soaddr);
}

//Retorna o numero do pacote
int numeroPacote(char *packet) {
	return (((packet[0] << 8) & 0xFF00) | (packet[1] & 0xFF)) & 0xFFFF;
}

//Retorna o tamanho do pacote
int tamanhoPacote(char *packet) {
	return (((packet[3] << 8) & 0xFF00) | (packet[4] & 0xFF)) & 0xFFFF;
}

//Retorna a flag do pacote (0 indica que ainda nao acabaram e 1 indica que esse e o ultimo pacote)
int flagDoPacote(char *packet) {
	return packet[2];
}

//Salva os dados recebidos e envia uma confirmacao ao servidor
int salvaArquivoEConfirma(char *buff, int *windowbegin, int windowend, int mtu, int windowlen, int *pacoteEsperado, FILE *file, int sock, so_addr *soaddr) {
	int pacoteinicial, tamanhopac, pacote;

	pacoteinicial = numeroPacote(&buff[*windowbegin]);
	tamanhopac = tamanhoPacote(&buff[*windowbegin]);
	fwrite(&buff[*windowbegin+5], sizeof(char), tamanhopac, file);
	enviaConfirmacao(buff, *windowbegin, 1, sock, soaddr);
	*pacoteEsperado = (*pacoteEsperado + 1) & 0xFFFF;
	//Se o pacote escrito era o ultimo a ser recebido, acabou
	if(flagDoPacote(&buff[*windowbegin]) == 1) {
		return 0;
	}
	//Se ainda existem pacotes na janela que chegaram em ordem errada, escre eles no disco
	while(*windowbegin != windowend) {
		//Anda com a janela uma posicao
		*windowbegin = (*windowbegin + mtu) % (windowlen * mtu);
		pacote = numeroPacote(&buff[*windowbegin]);
		//Se o pacote a ser escrito eh realmente o proximo, escreve ele no disco
		if(pacote != pacoteinicial + 1) {
			break;
		}
		pacoteinicial = pacote;
		tamanhopac = tamanhoPacote(&buff[*windowbegin]);
		fwrite(&buff[*windowbegin+5], sizeof(char), tamanhopac, file);
		enviaConfirmacao(buff, *windowbegin, 1, sock, soaddr);
		*pacoteEsperado = (*pacoteEsperado + 1) & 0xFFFF;
		if(flagDoPacote(&buff[*windowbegin]) == 1) {
			return 0;
		}
	}
	return 1;
}

//Recebe e escreve o arquivo de saida
unsigned int recebeArquivo(FILE *file, int sock, char *buff, int bufflen, int mtu, int windowlen, so_addr *soaddr) {
	int i, flagFinish, count, checksum, windowBegin, windowEnd, pacoteEsperado, packNum, packRange;
	unsigned int bytesRecebidos;

	bytesRecebidos = 0;
	flagFinish = 1;
	windowBegin = 0;
	windowEnd = 0;
	pacoteEsperado = 0;
	//Loop para receber as mensagens, enviar as confirmacoes e escrever os dados no disco
	while(flagFinish) {
		count = tp_recvfrom(sock, &buff[windowBegin], mtu, soaddr);
		checksum = checkCheckSum(&buff[windowBegin], tamanhoPacote(&buff[windowBegin]) + 7);
		//Se o checksum falhar, pede o reenvio pelo servidor
		if(checksum == FALSE) {
			enviaConfirmacao(buff, windowBegin, 0, sock, soaddr);
			flagFinish = 1;
			continue;
		}
		packNum = numeroPacote(&buff[windowBegin]);
		//Se for o pacote esperado, salva no arquivo e envia confirmacao
		if(packNum == pacoteEsperado) {
			bytesRecebidos += tamanhoPacote(&buff[windowBegin]);
			flagFinish = salvaArquivoEConfirma(buff, &windowBegin, windowEnd, mtu, windowlen, &pacoteEsperado, file, sock, soaddr);
			if(flagFinish == 0) break;
		}
		//Se nao foi o pacote esperado
		else {
			packRange = (pacoteEsperado + windowlen) & 0xFFFF;
			//Se o pacote eh duplicado, descarte-o
			if(packNum < packRange && packNum < pacoteEsperado) {}
			//Se o pacote nao cabe na janela, descarte-o
			else if(packNum > packRange && packNum > pacoteEsperado) {}
			//Se o pacote couber na janela, coloque-o na janela
			else {
				bytesRecebidos += tamanhoPacote(&buff[windowBegin]);
				if(packNum > pacoteEsperado) {
					i = pacoteEsperado - packNum;
				}
				else {
					i = packRange - packNum;
				}
				i *= mtu;
				i = i % windowlen;
				memcpy(&buff[windowBegin], &buff[i], count);
				windowEnd = (i + mtu) % windowlen;
			}
		}
	}

	//Retorna a quantidade de dados recebido
	return bytesRecebidos;
}

//Checa se uma mensagem foi recebida corretamente
BOOL checkCheckSum(char *buff, int bufflen) {
	int i;
	unsigned short count, checksum;

	bufflen -= 2;
	count = 0;
	for(i = 0; i < bufflen; i++) {
		count += ((unsigned short) buff[i]) & 0xFF;
	}

	checksum = (unsigned short)((buff[i] << 8) & 0xFF00) | (buff[i+1] & 0xFF);

	if(checksum == count) {
		return TRUE;
	}
	return FALSE;
}

//Cria o checksum para uma mensagem a ser enviada
void createCheckSum(char *buff, int bufflen) {
	int i;
	unsigned short checksum;

	checksum = 0;
	for(i = 0; i < bufflen; i++) {
		checksum += ((unsigned short) buff[i]) & 0xFF;
	}
	buff[i] = (checksum >> 8) & 0xFF;
	buff[i+1] = checksum & 0xFF;
}

//Imprime a diferenca entre os tempos tv1 e tv2
void imprimeDiferencaTempo(struct timeval *tv1, struct timeval *tv2, unsigned int bytesRecebidos, int bufflen) {
	long sec, usec;
	double kbps, totaltime;

	sec = tv2->tv_sec - tv1->tv_sec;
	usec = tv2->tv_usec - tv1->tv_usec;

	while(usec < 0) {
		usec += 1000000;
		sec -= 1;
	}

	totaltime = (double)sec + (double)usec/1000000;
	kbps = ((double)bytesRecebidos * 8) / totaltime;
	kbps = kbps / 1000;
	printf("Buffer = %5u byte(s), %10.2f kbps (%u bytes em %3ld.%06ld s)\n", bufflen, kbps, bytesRecebidos, sec, usec);
}