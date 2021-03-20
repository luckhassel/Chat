#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define LENGTH 2048

//Variável que verifica saída ou estadia do cliente.
//Atomica para poder ser acessada pelas threads
volatile sig_atomic_t flag = 0;
int sockfd = 0; //File descriptor inicial
char name[32]; //Armazena o nome do cliente

//Sobrepõe a saída inicial afim de melhorar aparência
void str_overwrite_stdout() {
  printf("%s", "> ");
  fflush(stdout);
}

//Verifica se houve quebra de linha e corrige isso implementando fim de string
void str_trim_lf (char* arr, int length) {
  int i;
  for (i = 0; i < length; i++) {
    if (arr[i] == '\n') { //Caso haja quebra de linha
      arr[i] = '\0'; //Seleciona o fim de string no lugar
      break;
    }
  }
}

//Verifica saída por atalho ctrl c para setar a flag de saída
void catch_ctrl_c_and_exit(int sig) {
    flag = 1;
}

//Lida com as mensagens de clientes
void send_msg_handler() {
	char message[LENGTH] = {}; //Variável que armazena a mensagem
	char buffer[LENGTH + 32] = {}; //Armazena o buffer e o nome
	int i = 0;

  //Loop principal da Thread
	while(1) {
	str_overwrite_stdout(); //Implementa a formatação no console
	fgets(message, LENGTH, stdin); //Realiza o get da mensagem
	str_trim_lf(message, LENGTH); //Verifica a quebra de linha

  //Caso o cliente queira sair
	if (strcmp(message, "exit") == 0) {
			break;
	} else {
		sprintf(buffer, "%s: %s\n", name, message);
		//Realiza a criptografia da mensagem. Aqui, foi utilizada a criptogragfia
		//do tipo Caesar Cypher, no qual todos os caracteres são deslocados um número x
		//na tabela ASC. No caso, a idéia foi deslocar três caracteres
		for (i=0; i<LENGTH + 32; i++){
			if (buffer[i] != '\0'){
				buffer[i] = buffer[i] + 3;
			}

		}
    //Envia a mensagem criptografada ao servidor
		send(sockfd, buffer, strlen(buffer), 0);
	}

	bzero(message, LENGTH); //Zera o buffer da mensagem
	bzero(buffer, LENGTH + 32); //Zera o buffer de saída
	}
	catch_ctrl_c_and_exit(2);
}

//Realiza o recebimento das mensagens
void recv_msg_handler() {
	char message[LENGTH] = {}; //Vetor de char para armazenar a mensagem
	int i=0;
  //Loop principal
  while (1) {
		int receive = recv(sockfd, message, LENGTH, 0); //Recebe a mensagem
    //Realiza a decriptação da mensagem, utilizando a mesma chave utilizada
    //para encriptar a mensagem <srv>
    		if (message[0] == 60 && message[1] == 115 && message[2] == 114 && message[3] == 118 && message[4] == 62){
    		}
    		else{
			for (i=0; i<LENGTH; i++){
				if (message[i] != '\0')
					message[i] = message[i] - 3;
			}
		}
    //Verifica o recebimento das mensagens
    if (receive > 0) {
      printf("%s", message);
      str_overwrite_stdout();
    } else if (receive == 0) {
			break;
    } else {
			// -1
		}
    //Set de memória
		memset(message, 0, sizeof(message));
  }
}

//Função principal, na qual deve receber a porta na qual deseja se conectar
int main(int argc, char **argv){
  //Verifica se a porta foi inserida
	if(argc != 2){
		printf("Port Error: %s is not available!\n", argv[0]);
		return EXIT_FAILURE;
	}

	char *ip = "127.0.0.1"; //IP da máquina que deseja se conectar
	int port = atoi(argv[1]); //Recebe a porta

	signal(SIGINT, catch_ctrl_c_and_exit); //Declara o signal ctrl_c

	printf("Please, enter your name: ");
  fgets(name, 32, stdin); //Recebe o nome do cliente
  str_trim_lf(name, strlen(name));

  //Verifica o nome da pessoa se está correto
	if (strlen(name) > 32 || strlen(name) < 2){
		printf("Name must be less than 30 and more than 2 characters.\n");
		return EXIT_FAILURE;
	}

	struct sockaddr_in server_addr; //Declara uma struct para armazenar dados do servidor

	//Configura informações do serviodr, como o endereço, porta, tipo de comunicação e protocolo
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(ip);
  server_addr.sin_port = htons(port);


  //Conecta ao servidor, se possível
  int err = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (err == -1) {
		printf("ERROR: Could not connect to the server\n");
		return EXIT_FAILURE;
	}

	//Envia o nome ao servidor
	send(sockfd, name, 32, 0);

	printf("=== WELCOME TO EMBEDDED WHATS ===\n");

  //Cria um objeto do tipo pthread_t
	pthread_t send_msg_thread;
    //Tenta criar a thread de envio de mensagens
  	if(pthread_create(&send_msg_thread, NULL, (void *) send_msg_handler, NULL) != 0){
		printf("ERROR: Could no start the send thread\n");
    return EXIT_FAILURE;
	}

  //Tenta criar a thread de recebimento de mensagens
	pthread_t recv_msg_thread;
  	if(pthread_create(&recv_msg_thread, NULL, (void *) recv_msg_handler, NULL) != 0){
		printf("ERROR: Could not start the receive thread\n");
		return EXIT_FAILURE;
	}

  //Fica verificando se a flag foi setada. Caso sim, fecha a conexão
	while (1){
		if(flag){
			printf("\nBye\n");
			break;
    }
	}

	close(sockfd);

	return EXIT_SUCCESS;
}
