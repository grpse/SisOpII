#include "dropboxUtil.h"

int read_until_eos_buffered(int sock, char *buffer)
{
	int i = 0;
	int r = 0;
	char buffered[1024];
	int buffer_size = sizeof(buffered);
	do
	{
		r = read(sock, buffered, buffer_size);
		memcpy((buffer + i), buffered, r);

		if (r < 0)
			return -1;
		else if (r == 0)
			break;
		else if (r < buffer_size)
			break;

		i += r;

	} while (1);

	return i;
}

int read_n_from_socket(int n, int sock, char *buffer)
{
	int i = 0;
	int r = 0;

	while (i < n)
	{
		r = read(sock, &buffer[i], 1);
		if (r < 0)
		{
			return -1;
		}
		i += r;
	}
}

int read_until_eos(int sock, char *buffer)
{
	int i = 0;
	int r = 0;

	do
	{
		r = read(sock, &buffer[i], 1);
		if (r < 0)
			return -1;
		i += r;
	} while (buffer[i - r]);

	return i;
}

int write_str_to_socket(int sock, char *str)
{
	return write(sock, str, strlen(str) + 1);
}

int read_and_save_to_file(int sock, char *filename, int fsize)
{
	int f = -1;
	if ((f = creat(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
		return -1;

	char buffer_copy[1];

	int k = 0, r;
	while (k < fsize)
	{
		r = read(sock, buffer_copy, sizeof(buffer_copy));
		if (r < 0)
			break;

		//printf("%c", c);
		r = write(f, buffer_copy, r);
		if (r < 0)
		{
			puts("Error writing file");
			close(f);
			return -1;
		}
		k += r;
	}
	
	close(f);
	return 1;
}

int read_and_save_to_file_and_callback(int sock, char *filename, int fsize, void (*on_each_read)(char *buffer, int size))
{
	int f = -1;
	if ((f = creat(filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
		return -1;

	char buffer_copy[2048];

	int k = 0, r;
	while (k < fsize)
	{
		r = read(sock, buffer_copy, sizeof(buffer_copy));
		if (on_each_read)
			on_each_read(buffer_copy, r);
		if (r < 0)
			break;

		//printf("%c", c);
		r = write(f, buffer_copy, r);
		if (r < 0)
		{
			puts("Error writing file");
			close(f);
			return -1;
		}
		k += r;
	}
	close(f);
	return 1;
}

int write_file_to_socket(int sock, char *filename, int fsize)
{
	int f = open(filename, O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (f < 0)
	{
		return -1;
	}
	int k = 0, r;
	//char c;

	char buffer_copy[2048];
	while (k < fsize)
	{
		r = read(f, buffer_copy, sizeof(buffer_copy));
		if (r < 0)
			break;
		r = write(sock, buffer_copy, r);
		if (r < 0)
			break;
		k += r;
	}
	
	close(f);
	return 1;
}

// Lê um numero do socket
int read_int_from_socket(int sock, int *number)
{
	char number_str[16];
	read_until_eos(sock, number_str);
	sscanf(number_str, "%d", number);
}

// cria um tcp server e executa threads com uma função para tratar novas conexões
// executa uma thread para cada nova conexão, mas bloqueia essa função.
int execute_tcp_server_listener_block(int port, void *(*execute_client)(void *args))
{
	int *returnStatus;
	pthread_t execution_thread = execute_tcp_server_listener_nonblock(port, execute_client);
	pthread_join(execution_thread, (void **)&returnStatus);

	return *returnStatus;
}

// cria um tcp server e executa threads com uma função para tratar novas conexões
// executa uma thread para cada nova conexão e não bloqueia a execução.
// Recebe uma função de callback para tratar a desconexão de um cliente.
pthread_t execute_tcp_server_listener_callback_nonblock(int port, void *(*execute_client)(void *args), void *(*client_disconnect_callback)(int client_socket))
{
	// inicia a estrutura para passar como parâmetro
	struct PortAndFunc *portAndFuncArgs = MALLOC1(struct PortAndFunc);
	portAndFuncArgs->port = port;
	portAndFuncArgs->execute_client = execute_client;
	portAndFuncArgs->client_disconnect_callback = client_disconnect_callback;

	// executa uma thread para tratar o executor de escuta do servidor TCP
	return async_executor(portAndFuncArgs, __execute_tcp_server_listener_nonblock);
}

// cria um tcp server e executa threads com uma função para tratar novas conexões
// executa uma thread para cada nova conexão e não bloqueia a execução.
pthread_t execute_tcp_server_listener_nonblock(int port, void *(*execute_client)(void *args))
{
	// inicia a estrutura para passar como parâmetro
	struct PortAndFunc *portAndFuncArgs = MALLOC1(struct PortAndFunc);
	portAndFuncArgs->port = port;
	portAndFuncArgs->execute_client = execute_client;
	portAndFuncArgs->client_disconnect_callback = NULL;

	// executa uma thread para tratar o executor de escuta do servidor TCP
	return async_executor(portAndFuncArgs, __execute_tcp_server_listener_nonblock);
}

// Executa um servidor TCP em segunda instância e aguada novas conexões para processar em outra thread
void *__execute_tcp_server_listener_nonblock(void *args)
{

	int returnStatus = 0;
	struct PortAndFunc portAndFuncArgs;

	// Obtém os argumentos fazendo um byte copy
	memcpy(&portAndFuncArgs, args, sizeof(struct PortAndFunc));
	free(args);

	int sockfd = create_tcp_server(portAndFuncArgs.port);
	if (sockfd < 0)
	{
		returnStatus = -1;
		pthread_exit(&returnStatus);
	}

	struct sockaddr_in cli_addr;
	socklen_t clilen = sizeof(struct sockaddr_in);

	while (true)
	{
		int *newsockfd = MALLOC1(int);
		*newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
		if (*newsockfd < 0)
		{
			printf("ERROR on accept");
			returnStatus = -1;
			pthread_exit(&returnStatus);
		}

		// Get client thread to make external cancelation
		pthread_t client_thread = async_executor(newsockfd, portAndFuncArgs.execute_client);

		// If there is a function to be called back on disconnection, start verifying
		// connection status to call it on the end of an connection
		if (portAndFuncArgs.client_disconnect_callback != NULL)
		{
			struct PortAndFunc *params = MALLOC1(struct PortAndFunc);
			params->client_socket = *newsockfd;
			params->client_thread = client_thread;
			async_executor(params, stay_verifying_socket_disconnection);
		}
	}

	close(sockfd);
	returnStatus = 0;
	pthread_exit(&returnStatus);
}

// Stays verifying if connected socket is still alive
// and call a callback function when disconnect
void *stay_verifying_socket_disconnection(void *args)
{
	// make arguments copy
	struct PortAndFunc params;
	memcpy(&params, args, sizeof(struct PortAndFunc));
	free(args);

	// watch for socket disconnection
	while (true)
		if (is_socket_disconnected(params.client_socket))
			break;

	// when disconnected, call the callback function with
	// client socket to handle it clearly
	if (params.client_disconnect_callback != NULL)
		params.client_disconnect_callback(params.client_socket);

	// cancel client thread
	pthread_cancel(params.client_thread);
}

/*
Conecta cliente ao servidor.
Recebe o host e a porta para fazer conexão.
retorna > 0 se a conexão ocorreu com sucesso.
*/
int connect_server(char *host, int port)
{
	struct sockaddr_in serv_addr;
	struct hostent *server;
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		return -1;

	// traduz o hostname de string para uma
	// struct hostent*
	server = gethostbyname(host);
	if (server == NULL)
		return -1;
	// preenche a estrutura de sockaddr...
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
	bzero(&(serv_addr.sin_zero), 8);
	// ... para fazer chamar a função connect
	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		return -1;

	return sockfd;
}

int create_tcp_server(int port)
{
	int sockfd;
	struct sockaddr_in serv_addr, cli_addr;

	// Cria o socket para o servidor
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("ERROR opening socket\n");
		return -1;
	}

	// Habilita a reconexão ao socket pela mesma porta
	int enable = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
	{
		perror("setsockopt(SO_REUSEADDR) failed");
		return -1;
	}

	// Preenche a estrutura para criar um socket tcp
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);

	//
	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR on binding\n");
		return -1;
	}

	if (listen(sockfd, 10) < 0)
	{
		perror("ERROR on listening\n");
		return -1;
	}

	return sockfd;
}

// executar função em outra thread sem precisar de um thread create
pthread_t async_executor(void *args, void *(*async_execute)(void *args))
{
	pthread_t t;
	pthread_create(&t, NULL, async_execute, args);
	return t;
}

/** 
 * Recebe por parâmetro de saída uma lista de ip's, separados por \n
 * atribuidos às interfaces ETHx e retorna a quantidade de ip's.
*/
int get_ip_list(char *ip_list)
{
	// clear number and ip list string
	int ip_list_count = 0;
	ip_list[0] = '\0';

	struct ifaddrs *ifaddr = NULL, *ifa = NULL;
	int family, s, n;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) == -1)
	{
		perror("getifaddrs");
		exit(EXIT_FAILURE);
	}

	/* Walk through linked list, maintaining head pointer so we
	can free list later */

	for (ifa = ifaddr, n = 0; ifa != NULL && ifa->ifa_addr != NULL; ifa = ifa->ifa_next, n++)
	{
		if (ifa->ifa_addr == NULL)
			continue;

		family = ifa->ifa_addr->sa_family;

		/* Display interface name and family (including symbolic
          form of the latter for the common families) */

		//printf("%-8s %s (%d)\n",
		//       ifa->ifa_name,
		//       (family == AF_PACKET) ? "AF_PACKET" :
		//       (family == AF_INET) ? "AF_INET" :
		//       (family == AF_INET6) ? "AF_INET6" : "???",
		//       family);

		/* For an AF_INET* interface address, display the address */

		if (family == AF_INET || family == AF_INET6)
		{
			s = getnameinfo(ifa->ifa_addr,
							(family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
							host, NI_MAXHOST,
							NULL, 0, NI_NUMERICHOST);
			if (s != 0)
			{
				printf("getnameinfo() failed: %s\n", gai_strerror(s));
				exit(EXIT_FAILURE);
			}

			// se for ipv4 e a interface for eth? (regex) adiciona o host na lista
			if (family == AF_INET && IS_IFACE_ETH(ifa->ifa_name))
			{
				// concatena um \n para separar caso mais de um ip
				// if (*ip_list_count > 0)
				// 	strcat(ip_list, "\n");
				// strcat(ip_list, host);
				// *ip_list_count = (*ip_list_count) + 1;

				// TODO: Create more ethernet interfaces support to send more ip address possibility
				strcpy(ip_list, host);
				ip_list_count = 1;
				return ip_list_count;
			}

			//printf("\t\taddress: <%s>\n", host);
		}
		else if (family == AF_PACKET && ifa->ifa_data != NULL)
		{
			struct rtnl_link_stats *stats = ifa->ifa_data;

			//printf("\t\ttx_packets = %10u; rx_packets = %10u\n"
			//       "\t\ttx_bytes   = %10u; rx_bytes   = %10u\n",
			//       stats->tx_packets, stats->rx_packets,
			//       stats->tx_bytes, stats->rx_bytes);
		}
	}

	freeifaddrs(ifaddr);
}

void get_peer_ip_address(int sock, char *ip_buffer)
{
	// assume s is a connected socket
	socklen_t len;
	struct sockaddr_storage addr;
	char ipstr[INET6_ADDRSTRLEN];
	int port;

	len = sizeof addr;
	getpeername(sock, (struct sockaddr *)&addr, &len);

	// deal with both IPv4 and IPv6:
	if (addr.ss_family == AF_INET)
	{
		struct sockaddr_in *s = (struct sockaddr_in *)&addr;
		port = ntohs(s->sin_port);
		inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof ipstr);
	}
	else
	{ // AF_INET6
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
		port = ntohs(s->sin6_port);
		inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof ipstr);
	}

	printf("Peer IP address: %s\n", ipstr);
	printf("Peer port      : %d\n", port);

	strcpy(ip_buffer, ipstr);
}

int is_socket_disconnected(int sockfd)
{
	int error = 0;
	socklen_t len = sizeof(error);
	int retval = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
	return retval != 0 || error != 0;
}

bool modify_file_time(char* filename, char* modtime_str)
{
	// Ajusta a hora de modificação para a hora local
    struct utimbuf ntime;
    struct tm modtime = {0};
    // formata a data para "%Y-%m-%D %H:%M:%S"
    strptime(modtime_str, "%F %T", &modtime);
    // transforma em tempo numérico e preenche a estrutura
    time_t modif_time = mktime(&modtime);
    ntime.actime = modif_time;
    ntime.modtime = modif_time;
    // salva a nova data de modificação no arquivo feito o download
	return utime(filename, &ntime) >= 0;
}

void path_join_buffered(char *buffer, ...)
{
	char *DIR_SEPARATOR = "/";
	char *str;
	bzero(buffer, PATH_MAX);

	va_list arg;

	va_start(arg, str);

	while (str)
	{

		str = va_arg(arg, char *);
		if (str == NULL)
			break;
		// concat new path
		strcat(buffer, str);
		// if it already has DIR_SEPARATOR as last char,
		// don't append it
		int str_length = strlen(str);
		if (str[str_length - 1] == DIR_SEPARATOR[0])
			continue;

		// else concatenate it to the end of the string
		strcat(buffer, DIR_SEPARATOR);
	}

	va_end(arg);

	// // start variables
	// char* DIR_SEPARATOR = "/";
	// int i = 0;
	// size_t max_paths = PATH_MAX;

	// // clear buffer
	// bzero(buffer, max_paths);

	// va_list vl;
	// va_start(vl, max_paths);

	// for (i = 0;i < max_paths;i++)
	// {
	// 	// get next string from arguments
	// 	char* val = va_arg(vl, char*);
	// 	if (val == NULL) break;
	// 	// concat new path
	// 	strcat(buffer, val);
	// 	// if it already has DIR_SEPARATOR as last char,
	// 	// don't append it
	// 	int val_length = strlen(val);
	// 	if (val[val_length - 1] == DIR_SEPARATOR[0]) continue;

	// 	// else concatenate it to the end of the string
	// 	strcat(buffer, DIR_SEPARATOR);

	// }
	// va_end(vl);
}

int file_copy(char* source, char* destination)
{
	// FILE* source_file = fopen(source, "rb");
	// FILE* destination_file = fopen(destination, "wb");
	// char c;
	// int byte_count = 0;
	// while((c = fgetc (source_file)) != EOF) 
	// {
	// 	byte_count++;
	// 	fputc(c, destination_file);
	// }

	char so_command[PATH_MAX];

	strcpy(so_command, "mv ");
	strcat(so_command, source);
	strcat(so_command, " ");
	strcat(so_command, destination);
	return system(so_command);

	//printf("BYTES COPYED: %d\n", byte_count);
	// return ferror(source_file) || ferror(destination_file) ? -1 : 1;
}