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
#define FILEHEADERSIZE 7
#define FILEANSWERSIZE 5

//Inicia a conexao com o cliente, recebendo o tamanho da janela dele
BOOL iniciaConexao(int sock, int *clientWindowlen, int bufflen, so_addr *soaddr);
//Recebe o nome do arquivo do cliente
BOOL recebeString(int sock, char *buff, int mtu, so_addr *soaddr, char **fileName);
//Envia o conteudo do arquivo para o cliente
void enviaArquivo(char *fileName, int sock, char *buff, int bufflen, int mtu, int windowSize, so_addr *soaddr);
//Realiza a verificacao do checksum
BOOL checkCheckSum(char *buff, int bufflen);
//Cria o campo checksum de uma mensagem
void createCheckSum(char *buff, int bufflen);

int main(int argc, char* argv[]) {
	int status, sock, bufflen, windowlen, clientWindowlen, i, mtu;
	char *fileName, *buff;
	so_addr soaddr;
	struct timeval timeout;

	if(argc != 4) {
		printf("Argumentos necessarios: 'porto do servidor' 'tamanho do buffer' 'tamanho da janela'.\n");
		exit(1);
	}
	
	//Cria o buffer
	windowlen = atoi(argv[3]);

	bufflen = atoi(argv[2]);
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
	sock = tp_socket((unsigned short) atoi(argv[1]));
	if(sock < 0) {
		printf("Erro ao criar o socket do servidor!\n");
		exit(5);
	}

	//Recebe o tamanho da janela do cliente
	status = iniciaConexao(sock, &clientWindowlen, bufflen, &soaddr);
	if(status < 0) {
		printf("Erro ao tentar receber o tamanho da janela do cliente!\n");
		exit(6);
	}
	
	//Recebe o nome do arquivo
	status = recebeString(sock, buff, mtu, &soaddr, &fileName);
	if(status < 0) {
		printf("Erro ao tentar receber o nome do arquivo!\n");
		exit(7);
	}

	//Utiliza o tamanho da menor janela para enviar os dados
	clientWindowlen = windowlen < clientWindowlen ? windowlen : clientWindowlen;

	//Timeout
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	//Envia o conteudo do arquivo para o cliente
	enviaArquivo(fileName, sock, buff, bufflen, mtu, clientWindowlen, &soaddr);
	
	return 0;
}

//Inicia a conexao com o cliente, recebendo o tamanho da janela dele
BOOL iniciaConexao(int sock, int *clientWindowlen, int bufflen, so_addr *soaddr) {
	int returnv, flagRecebido, checksum;
	char resposta[6], envio;

	flagRecebido = 1;
	while(flagRecebido) {
		//Recebe o tamanho da janela do cliente
		returnv = tp_recvfrom(sock, resposta, sizeof(resposta), soaddr);
		//Verifica se a mensagem chegou corretamente
		checksum = checkCheckSum(resposta, sizeof(resposta));
		//Se chegou
		if(checksum == TRUE) {
			flagRecebido = 0;
			envio = 0;
			//Confirma para o cliente que a mensagem chegou
			tp_sendto(sock, &envio, sizeof(envio), soaddr);
			break;
		}
		//Se nao
		else {
			flagRecebido = 1;
			envio = -1;
			//Pede para que o cliente reenvie a informacao
			tp_sendto(sock, &envio, sizeof(envio), soaddr);
			break;
		}
	}

	//Retorna o tamanho da janela do cliente
	*clientWindowlen = ((resposta[0] << 24)&0xFF000000) | ((resposta[1] << 16)&0xFF0000) | ((resposta[2] << 8)&0xFF00) | ((resposta[3])&0xFF);

	return returnv;
}

//Recebe o nome do arquivo do cliente
BOOL recebeString(int sock, char *buff, int mtu, so_addr *soaddr, char **fileName) {
	int returnv, flagRecebido, checksum, i, bytesleft, stringPointer;
	char resposta;
	char *nomeArquivo;

	flagRecebido = 1;
	while(flagRecebido) {
		//Recebe o primeiro pacote
		returnv = tp_recvfrom(sock, buff, mtu, soaddr);
		//Verifica o checksum
		checksum = checkCheckSum(buff, returnv);
		if(checksum == TRUE) {
			resposta = 0;
			//Envia a confirmacao de pacote correto
			tp_sendto(sock, &resposta, sizeof(resposta), soaddr);
			flagRecebido = 0;
			break;
		}
		else {
			resposta = -1;
			//Pede para que o cliente reenvie o pacote
			tp_sendto(sock, &resposta, sizeof(resposta), soaddr);
			flagRecebido = 1;
		}
	}

	//Verifica se ainda existem pacotes chegando
	bytesleft = ((buff[0] << 8)&0xFF00) | (buff[1]&0xFF);

	//Cria o buffer do tamanho do nome do arquivo
	nomeArquivo = malloc(sizeof(char)*(bytesleft + returnv - 4));
	stringPointer = 0;
	//Copia o nome do arquivo para o buffer
	for(i = 2; i < returnv - 2; i++) {
		nomeArquivo[stringPointer] = buff[i];
		stringPointer++;
	}

	flagRecebido = 0;
	//Enquanto existirem pacotes com o nome do arquivo para chegar
	while(flagRecebido || bytesleft > 0) {
		//Recebe o pacote
		returnv = tp_recvfrom(sock, buff, mtu, soaddr);
		checksum = checkCheckSum(buff, returnv);
		if(checksum == TRUE) {
			flagRecebido = 0;
			resposta = 0;
			//Envia a confirmacao de pacote correto
			tp_sendto(sock, &resposta, sizeof(resposta), soaddr);
		}
		else {
			flagRecebido = 1;
			resposta = -1;
			//Pede para que o cliente reenvie o pacote
			tp_sendto(sock, &resposta, sizeof(resposta), soaddr);
			continue;
		}
		//Verifica se ainda existem pacotes chegando
		bytesleft = ((buff[0] << 8)&0xFF00) | (buff[1]&0xFF);

		//Copia o nome do arquivo para o buffer
		for(i = 2; i < returnv - 2; i++) {
			nomeArquivo[stringPointer] = buff[i];
			stringPointer++;
		}
	}

	//Atualiza o nome do arquivo na funcao principal
	*fileName = nomeArquivo;

	return returnv;
}

//Funcao usada para criar um pacote formatado para ser enviado ao cliente
void criarPacote(char *packet, char *buff, int buffSize, int dataSize, int packNum, char flags) {
	int i, j, small, packSize;

	//Os primeiros 2 bytes indicam o numero do pacote
	packet[0] = (packNum >> 8) & 0xFF;
	packet[1] = (packNum & 0xFF);
	//O proximo byte indica se o pacote e o ultimo(1) ou nao(0)
	packet[2] = flags;
	j = 5;
	packSize = 0;
	small = (buffSize < dataSize) ? buffSize : dataSize;
	//Copia os dados do arquivo para dentro do pacote e mantem o tamanho da mensagem atualizado
	for(i = 0; i < small; i++) {
		packet[j] = buff[i];
		if(buff[i] == EOF) {
			packSize = i;
		}
		j++;
	}
	//Atualiza o tamanho da mensagem
	if(packSize == 0) {
		packSize = j - 5;
	}
	//Coloca o tamanho da mensagem dentro do pacote
	packet[3] = (packSize >> 8) & 0xFF;
	packet[4] = (packSize & 0xFF);
	//Cria o checksum do pacote
	createCheckSum(packet, packSize + 5);
}

//Verifica se o pacote de confirmacao do cliente esta confirmando uma mensagem
BOOL verificaPacote(char *packet) {
	//Se o checksum estiver correto
	if(checkCheckSum(packet, FILEANSWERSIZE) == TRUE) {
		//E o terceiro byte for 0, o pacote esta confirmado
		if(packet[2] == 0) {
			return TRUE;
		}
		//Se nao for 0, o pacote esta errado e deve ser reenviado
		else {
			return FALSE;
		}
	}
	//Se o checksum falhar, o pacote deve ser reenviado
	else {
		return FALSE;
	}
}

//Retorna o numero de um pacote
int numeroPacote(char *packet) {
	return (((packet[0] << 8) & 0xFF00) | (packet[1] & 0xFF)) & 0xFFFF;
}

//Arruma o vetor utilizado para fazer a janela deslizante
void deslizaJanela(int *window, int windowSize) {
	int i, j, start;

	for(i = 0; i < windowSize; i++) {
		if(window[i] != -1) {
			break;
		}
	}
	if(i == 0) {
		return;
	}
	start = i;
	j = 0;
	for(i = start; i < windowSize; i++) {
		window[j] = window[i];
	}
	for(i = windowSize - start; i < windowSize; i++) {
		window[i] = -1;
	}
}

//Retorna o numero do pacote mais antigo que ainda esteja na janela
int pacoteMaisAntigo(int *window, int windowSize) {
	int i;

	for(i = 0; i < windowSize; i++) {
		if(window[i] != -1) {
			return window[i];
		}
	}

	return -1;
}

//Envia o conteudo do arquivo para o cliente
void enviaArquivo(char *fileName, int sock, char *buff, int bufflen, int mtu, int windowSize, so_addr *soaddr) {
	FILE *arquivo;
	int bytesRead, packSend, i, packSize, lastPacket;
	int packNum, buffWindowBegin, packReceived, firstPackSend, buffPackToResend, recvValue;
	int *window;
	char *packet;

	//Cria a area de memoria utilizada para enviar os dados para o cliente
	packet = malloc(mtu*sizeof(char));
	if(packet == NULL) {
		printf("Nao foi possivel criar o buffer de envio de dados.\n");
		return;
	}
	//Abre o arquivo para leitura
	arquivo = fopen(fileName, "r");
	if(arquivo == NULL) {
		printf("Nao foi possivel abrir o arquivo para leitura.\n");
		//Se nao for possivel abrir o arquivo, envia uma mensagem para o cliente
		memset(packet, 0, sizeof(packet));
		criarPacote(packet, packet, 0, 0, 0, 1);
		tp_sendto(sock, packet, mtu, soaddr);
		return;
	}
	//Cria o vetor utilizado pela janela deslizante
	window = malloc(windowSize*sizeof(int));
	if(window == NULL) {
		printf("Nao foi possivel criar o buffer da janela deslizante.\n");
		return;
	}
	//Inicializa o vetor da janela deslizante
	for(i = 0; i < windowSize; i++) {
		window[i] = -1;
	}
	packNum = 0;
	buffWindowBegin = 0;
	firstPackSend = 0;
	lastPacket = -1;
	packSend = 0;
	packSize = mtu - FILEHEADERSIZE;
	//Enquanto o arquivo nao for totalmente lido e enviado
	while(!feof(arquivo)) {
		//Le um buffer por vez do arquivo
		bytesRead = fread(buff, sizeof(char), bufflen, arquivo);
		//Se o final do arquivo for encontrado, seta a variavel de lastPacket
		if(bytesRead < bufflen) {
			lastPacket = bytesRead / packSize;
			buff[bytesRead] = EOF;
		}
		buffWindowBegin = 0;
		//Loop para enviar uma janela completa de dados para o cliente
		while(buffWindowBegin < bytesRead) {
			firstPackSend = packNum;
			packSend = 0;
			//Envia os pacotes para o cliente
			for(i = buffWindowBegin; i < windowSize*packSize + buffWindowBegin; i += packSize) {
				if(i >= bytesRead) {
					break;
				}
				memset(packet, 0, sizeof(packet));
				//Envia os pacotes para o cliente, dependendo se e o ultimo ou nao
				if(i/packSize == lastPacket) {
					criarPacote(packet, &buff[i], bufflen - i, packSize, packNum, 1);
				}
				else {
					criarPacote(packet, &buff[i], bufflen - i, packSize, packNum, 0);
				}
				tp_sendto(sock, packet, mtu, soaddr);
				//Coloca o pacote enviado na janela deslizante
				window[packSend] = packNum;
				//Atualiza a variavel de numero dos pacotes
				packNum = (packNum + 1) % 0xFFFF;
				packSend++;
			}
			//Recebe as confirmacoes do cliente
			for(i = 0; i < packSend; i++) {
				memset(packet, 0, sizeof(packet));
				recvValue =  tp_recvfrom(sock, packet, mtu, soaddr);
				//Se confirmacao correta, anda com a janela, se puder
				if(recvValue != -1 && verificaPacote(packet) == TRUE) {
					packReceived = numeroPacote(packet);
					window[packReceived - firstPackSend] = -1;
				}
				//Se ocorreu um timeout, reenvia o pacote
				else if(recvValue == -1) {
					//Envia o pacote mais antigo que estiver na janela
					packReceived = pacoteMaisAntigo(window, windowSize);
					buffPackToResend = packReceived - firstPackSend + buffWindowBegin;
					memset(packet, 0, sizeof(packet));
					if(buffPackToResend == lastPacket) {
						criarPacote(packet, &buff[buffPackToResend], bufflen - buffPackToResend, packSize, packReceived, 1);
					}
					else {
						criarPacote(packet, &buff[buffPackToResend], bufflen - buffPackToResend, packSize, packReceived, 0);
					}
					tp_sendto(sock, packet, mtu, soaddr);
					i--;
				}
				//Se nao recebeu corretamente, reenvia o pacote
				else {
					//Reenvia o pacote que o cliente falou que chegou errado
					packReceived = numeroPacote(packet);
					buffPackToResend = packReceived - firstPackSend + buffWindowBegin;
					memset(packet, 0, sizeof(packet));
					if(buffPackToResend == lastPacket) {
						criarPacote(packet, &buff[buffPackToResend], bufflen - buffPackToResend, packSize, packReceived, 1);
					}
					else {
						criarPacote(packet, &buff[buffPackToResend], bufflen - buffPackToResend, packSize, packReceived, 0);
					}
					tp_sendto(sock, packet, mtu, soaddr);
					i--;
				}
			}
			buffWindowBegin += packSend*packSize;
			//Desliza a janela, se possivel
			deslizaJanela(window, windowSize);
		}
	}
	fclose(arquivo);
}

//Realiza a verificacao do checksum
BOOL checkCheckSum(char *buff, int bufflen) {
	int i;
	unsigned short checksum, count;

	bufflen -= 2;
	count = 0;
	for(i = 0; i < bufflen; i++) {
		count += ((unsigned short) buff[i]) & 0xFF;
	}
	
	checksum = ((buff[i] << 8) & 0xFF00) | (buff[i+1] & 0xFF);

	if(checksum == count) {
		return TRUE;
	}
	return FALSE;
}

//Cria o campo checksum de uma mensagem
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