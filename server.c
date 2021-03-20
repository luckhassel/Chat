#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

//Define o número máximo de clientes
#define MAX_CLIENTS 100
//Define o tamanho máximo do Buffer
#define BUFFER_SZ 2048

//Variável utilizada para gravar numero de clientes
//Variável atomica para poder ser acessado por todas as Threads
static _Atomic unsigned int cli_count = 0;
//Variável para gravar UID estático
static int uid = 10;

//Estrutura do cliente, no qual armazena informações como nome, endereço, uid e file descriptor
typedef struct{
	struct sockaddr_in address;
	int sockfd;
	int uid;
	char name[32];
} client_t;

//Cria um vetor de clientes do tipo client_t (Struct)
client_t *clients[MAX_CLIENTS];

//Criado um mutex para realizar sincronias de acesso a variáveis, fazendo com que o acesso
//Só seja realizado caso permitido
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

//Sobrescreve a saída padrão
void str_overwrite_stdout() {
    printf("\r%s", "> ");
    fflush(stdout);
}

//Realiza alguns ajustes caso haja quebra de linha ou algo parecido
//Basicamente deixa a mensagem mais legivel
void str_trim_lf (char* arr, int length) {
  int i;
  for (i = 0; i < length; i++) {
    if (arr[i] == '\n') { //Verifica a existência da quebra de linha
      arr[i] = '\0'; //Sibstitui por um indicativo de final de string
      break;
    }
  }
}

//Função auxiliar utilizada quando a sala está lotada
//Imprime informações sobre o cliente que deseja conectar
void print_client_addr(struct sockaddr_in addr){
    printf("%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xff, //Realiza operação lógica para legibilidade
        (addr.sin_addr.s_addr & 0xff00) >> 8, //Junto com a função &, desloca 8 bytes
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

//Adiciona um cliente à fila.
//Faz uso do mutex para evitar não sincronia
void queue_add(client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(!clients[i]){
			clients[i] = cl;
			break;
		}
	}

	pthread_mutex_unlock(&clients_mutex); //Desbloqueia a thread
}

//Remove cliente da fila. Serve para liberar espaço, caso alguém deseje
//Se conectar ao servidor
//Também faz uso do mutex para garantir sincronia
void queue_remove(int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid == uid){ //Verifica o nome do cliente para exclusão
				clients[i] = NULL; //Substitui o cliente por nulo
				break;
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex); //Desbloqueia a thread
}

//Envia mensagem para todos os clientes, exceto quem enviou
//Utiliza mutex para garantir sincronia
void send_message(char *s, int uid){
	pthread_mutex_lock(&clients_mutex); //Bloqueia a thread

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i]){ //Verifica a existência de clientes
			if(clients[i]->uid != uid){ //Caso não seja quem enviou
				if(write(clients[i]->sockfd, s, strlen(s)) < 0){ //Envia a mensagem
					perror("ERROR: write to descriptor failed");
					break;
				}
			}
		}
	}

	pthread_mutex_unlock(&clients_mutex); //Desbloqueia a thread
}

//Thread que lida com a comunicação dos clientes (Principal)
void *handle_client(void *arg){
	char buff_out[BUFFER_SZ]; //Buffer de saída
	char name[32]; //Nome do cliente
	int leave_flag = 0; //Flag que auxilia na saída de clientes do chat

	cli_count++;
	client_t *cli = (client_t *)arg; //Recebe a struct com dados do cliente

	//Verifica possíveis erros no nome do cliente
	if(recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) <  2 || strlen(name) >= 32-1){
		printf("ERROR - Something went wrong with your name!\n");
		leave_flag = 1; //Seta a flag de saída
	} else{
		strcpy(cli->name, name); //Coloca o nome gravado no char na struct
		sprintf(buff_out, "<srv> %s joined the conversation\n", cli->name); //Escreve a mensagem de entrada no buffer
		printf("%s", buff_out); //Escreve o buffer no servidor
		send_message(buff_out, cli->uid); //Envia a mensagem de entrada a todos os clientes
	}

	bzero(buff_out, BUFFER_SZ); //Zera o buffer de saída

	//Loop principal da thread, o qual olha por mensagem recebidas e encaminha a todos
	while(1){
		//Verifica se houve erro e sai do loop
		if (leave_flag) {
			break;
		}

		int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0); //Verifica entrada de novas mensagens
		if (receive > 0){
			if(strlen(buff_out) > 0){
				//Encaminha a mensagem recebida a todos os clientes
				send_message(buff_out, cli->uid);

				str_trim_lf(buff_out, strlen(buff_out));
				printf("%s \n",buff_out);
			}
		} else if (receive == 0 || strcmp(buff_out, "exit") == 0){ //Verifica se a mensagem é a flag de saída
			sprintf(buff_out, "<srv> %s left the conversation\n", cli->name);
			printf("%s", buff_out);
			send_message(buff_out, cli->uid); //Encaminha a mensagem de saída a todos os clientes
			leave_flag = 1; //Seta a flag de saída
		} else {
			printf("ERROR: -1\n");
			leave_flag = 1;
		}

		bzero(buff_out, BUFFER_SZ); //Zera o buffer
	}

	close(cli->sockfd); //Fecha a conexão do cliente
  queue_remove(cli->uid); //Remove o cliente da lista
  free(cli); //Libera espaço de memória da struct
  cli_count--; //Diminui o contador de clientes
  pthread_detach(pthread_self()); //Realiza o detach da thread

	return NULL;
}

//Função principal. Recebe o argumento da porta que deseja ser utilizada
int main(int argc, char **argv){
	if(argc != 2){
		printf("Port Error: %s is not available!\n", argv[0]);
		return EXIT_FAILURE;
	}

	char *ip = "127.0.0.1"; //Seleciona o endereço de IP
	int port = atoi(argv[1]); //Pega a porta entrada pelo usuário
	//Variáveis auxiliares
	int option = 1;
	int listenfd = 0, connfd = 0;
  struct sockaddr_in serv_addr; //Struct do tipo sockaddr_in para armazenar infos do servidor
  struct sockaddr_in cli_addr; //Struct do tipo sockaddr_in para armazenar infos do cliente
  pthread_t tid; //Cria um objeto do tipo thread

  //Configurações do servidor, como tipo de comunicação, protocolo, endereço de IP e porta
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(ip);
  serv_addr.sin_port = htons(port);

  //Ignora outros sinais de PIPE que podem vir a ocorrer
	signal(SIGPIPE, SIG_IGN);

	if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEADDR),(char*)&option,sizeof(option)) < 0){
		perror("ERROR: setsockopt failed");
    return EXIT_FAILURE;
	}

	//Tentativa de realizar o bind
  if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("ERROR: Socket bind failed. Please verify your port and IP address");
    return EXIT_FAILURE;
  }

  //Tentativa de iniciar a escuta do servidor
  if (listen(listenfd, 10) < 0) {
    perror("ERROR: Could not start listening. Please, verify your port and IP address");
    return EXIT_FAILURE;
	}

	printf("=== WELCOME TO EMBEDDED WHATS ===\n");

	//Loop que fica na escuta por novos clientes
	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		//Verifica se o número máximo de clientes foi atingido e imprime informações de quem tentou se conectar
		if((cli_count + 1) == MAX_CLIENTS){
			printf("ERROR: Room is full. Please, try again later or contact the admin");
			print_client_addr(cli_addr);
			printf(":%d\n", cli_addr.sin_port);
			close(connfd);
			continue;
		}

		//Configura elementos do cliente, como endereço, file descriptor e ID
		client_t *cli = (client_t *)malloc(sizeof(client_t));
		cli->address = cli_addr;
		cli->sockfd = connfd;
		cli->uid = uid++;

		//Adiciona o cliente a fila
		queue_add(cli);
		//Realiza a criação da thread que realiza o encaminhamento das mensagens
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		//Evita uso do processador
		sleep(1);
	}

	return EXIT_SUCCESS;
}
