#include <sys/socket.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>


#include "FtpServer.h"



std::map<int , bool> gConnectIsExistMap;
std::map<string , CmdHandleFunction> gCmdToFunctionMap;
std::map<int , int> gEventFdToIntMap;
std::map<int , FtpConnect *> gFdToFtpConnectMap;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/*
	argument: FtpServer *pServer 
	Describe: 1.init pServer's serverSocket and bind it
*/
void init_FtpServer(FtpServer *pServer)
{

	if(pServer->serverSocket > 0)
		close(pServer->serverSocket);

	pServer->serverSocket = socket(AF_INET , SOCK_STREAM, 0);

	if(pServer->serverSocket < 0)
	{
		DebugInfo("socket error ,the error = %d\n", errno);
		exit(-1);
	}

	DebugInfo("the Socket is %d\n", pServer->serverSocket);

	bzero(&pServer->serverAddr,sizeof(struct sockaddr));
	pServer->serverAddr.sin_family = AF_INET;
	pServer->serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	pServer->serverAddr.sin_port = htons(pServer->port);

	strcpy(pServer->_ip , inet_ntoa(pServer->serverAddr.sin_addr));

	if(bind(pServer->serverSocket , (struct sockaddr *)&pServer->serverAddr 
		, sizeof(struct sockaddr)) < 0)
	{
		DebugInfo("bind error , the error = %d\n", errno);
		exit(-1);
	}

	pServer->epfd = epoll_create(MaxEpollCount);
		
	DebugInfo("init FtpServer success\n");

	gEventFdToIntMap[pServer->serverSocket] = SERVER;
	gEventFdToIntMap[STDIN_FILENO] = SCREENIN;

	gCmdToFunctionMap["Test"] = &CmdTest;
	gCmdToFunctionMap["PWD"]  = &HandlePwd;
	gCmdToFunctionMap["CWD"]  = &HandleCwd;
	gCmdToFunctionMap["PORT"] = &HandlePort;
	gCmdToFunctionMap["LIST"] = &HandleList;
	gCmdToFunctionMap["PASV"] = &HandlePASV;
	gCmdToFunctionMap["STOR"] = &HandleSTOR;
	gCmdToFunctionMap["RETR"] = &HandleRETR;
	
}

/*
	argument: FtpServer *pServer
	Describe: 1.listen pServer's serverSocket and for 
			    accept's connect build thread to deal 
		        with cmd and data
*/
void start_FtpServer(FtpServer *pServer)
{
	listen(pServer->serverSocket , MaxListenCount);
	addFdToEpoll(pServer->epfd, pServer->serverSocket);
	addFdToEpoll(pServer->epfd, STDIN_FILENO);

	while(1)
	{
		struct epoll_event events[32];
		int len = epoll_wait(pServer->epfd , events , 32 , -1);
		for(int i = 0 ; i<len ; i++)
		{
			switch (gEventFdToIntMap[events[i].data.fd])
			{
				case SERVER:
					if(events[i].events & EPOLLIN)
					{
						FtpServerHandleSocket(pServer);
					}
					break;
				case SCREENIN:
					if(events[i].events & EPOLLIN)
					{
						FtpServerHandleStdin(pServer);
					}
					break;
				default:
					if(gConnectIsExistMap[events[i].data.fd])
					{
						if(events[i].events & EPOLLIN)
						{
							initFtpConnect(gFdToFtpConnectMap[events[i].data.fd] , pServer , events[i].data.fd);
						}
					}
					else
					{
						DebugInfo("The event don't deal with\n");
					}
					DebugInfo("events[i].data.fd is :%d\n" , events[i].data.fd);
					break;
			}

		}
	}
}


void ExitFtpServer(FtpServer *pServer)
{
	DebugInfo("FtpServer is End\n");
	std::map<int, FtpConnect *>::iterator iter;  
    for(iter = gFdToFtpConnectMap.begin(); iter != gFdToFtpConnectMap.end(); iter++)
    {
    	free(iter->second);
    }
	free(pServer);
}


/*
	argument: FtpServer *pServer
	Describe: 1.deal with STDIN's data
			  2.if exit must free memory
*/
void FtpServerHandleStdin(FtpServer *pServer)
{
	char readBuff[128];
	int bufflen = read(STDIN_FILENO,readBuff , sizeof(readBuff));
	readBuff[bufflen - 1] = 0; 
	if(strcmp(readBuff , "quite") == 0)
	{
		ExitFtpServer(pServer);
		exit(0);
	}
}

/*
	argument: FtpServer *pServer
	Describe: 1.deal with Socket's data
			  2.build new connect and open 
			    thread to deal with cmd and data
*/
void FtpServerHandleSocket(FtpServer *pServer)
{
	struct sockaddr_in connectedAddr;
	char connectIp[20];
	socklen_t sockAddrLen = sizeof(struct sockaddr_in);

	int connFd = accept(pServer->serverSocket , NULL , NULL);
	
	getpeername(connFd, (struct sockaddr *)&connectedAddr, &sockAddrLen);
	strcpy(connectIp , inet_ntoa(connectedAddr.sin_addr));

	DebugInfo("have a new connect , The connectIp is :%s\n" , connectIp);
	DebugInfo("local Ip is :%s\n" , pServer->_ip);

	addFdToEpoll(pServer->epfd, connFd);

	//need add lock
	pthread_mutex_lock(&mutex);
	gConnectIsExistMap[connFd] = true;
	pthread_mutex_unlock(&mutex);
	
}



/*
	argument: FtpConnect *pConnect
			  FtpServer *server       
			  int remoteSocket        ---client's socket
	Describe: 1.init struct FtpConnect
			  2.start thread

*/
void initFtpConnect(FtpConnect *pConnect , FtpServer *server , int remoteSocket)
{
	if(pConnect == NULL)
	{
		pConnect = (FtpConnect *)malloc(sizeof(FtpConnect));
		pConnect->remoteSocket = remoteSocket;
		pConnect->parentCon = server;
		getcwd(pConnect->path, sizeof(pConnect->path));
		strcpy(pConnect->serverIp , server->_ip);
		
		pthread_mutex_lock(&mutex);
		gFdToFtpConnectMap[remoteSocket] = pConnect;
		pthread_mutex_unlock(&mutex);
	}
	pthread_create(&pConnect->thread_id , NULL , Connect_Thread, (void *)pConnect);
}

/*
	argument: void *data     ------ Start the thread of data
	Describe: 1.init struct FtpConnect
			  2.start thread

*/
void *Connect_Thread(void *data)
{
	FtpConnect *pConnect = (FtpConnect *)data;
	DebugInfo("%d is start success\n" , (unsigned int)pthread_self());
	WaitCmdToHandle(pConnect);
	return NULL;
}

/*
	argument: FtpConnect *connect     -----the current connect's struct
	Describe: 1.Get the remote's cmd and Deal with it
*/
void WaitCmdToHandle(FtpConnect *connect)
{
	char *buff = (char*) malloc(sizeof(char) * 1000);
	string data;
	Recv_msg_Resolve(connect , &buff, &data);
	string cmd(buff);
	DebugInfo("the buff msg is %s\n" , cmd.c_str());
	if(gCmdToFunctionMap[cmd] != NULL)
	{
		gCmdToFunctionMap[cmd](connect , (void *)&data);
	}
	else
	{
		DebugInfo("Cmd is Error\n");
	}
	

}


/*
	argument: int clientSocket     -----the client's socket
			  char **buff 		   -----return cmd msg
			  string *data		   -----return the data msg
	Describe: 1.Get the remote's cmd and Deal with it
*/

void Recv_msg_Resolve(FtpConnect *con,char **buff , string *data)
{
	memset(*buff, 0 , BUFFER_SIZE * sizeof(char));
	int n = recv(con->remoteSocket, *buff, BUFFER_SIZE, 0);
	if(n == 0)
	{
		DebugInfo("socket is close\n");
		pthread_mutex_lock(&mutex);
		gFdToFtpConnectMap[con->remoteSocket] = NULL;
		gConnectIsExistMap.erase(con->remoteSocket);
		pthread_mutex_unlock(&mutex);
		delFdFromEpoll(con->parentCon->epfd, con->remoteSocket);
		free(con);
		pthread_exit(NULL);
	}
	DebugInfo("Recv data is :%s\n" , *buff);
	
	string msg(*buff);
	int index = msg.find_first_of(" ");

	(*data).assign(msg , index + 1 , n);
	(*buff)[index] = '\0';
	
	DebugInfo("By Resolve the buff is :%s\n" , *buff);
	DebugInfo("By Resolve the data is :%s\n" , (*data).c_str());
}


/*
	argument: int epfd, int fd
			  epfd ---- epoll_create return value 
	Describe: 1.EpollEvent with fd add to epfd
*/
void addFdToEpoll(int epfd,int fd)
{
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = fd;
	epoll_ctl(epfd , EPOLL_CTL_ADD , fd , &event);
}

/*
	argument: int epfd, int fd
			  epfd ---- epoll_create return value 
	Describe: 1.EpollEvent with fd del for epfd
*/
void delFdFromEpoll(int epfd,int fd)
{
	struct epoll_event event;
	event.events = EPOLLIN | EPOLLET;
	event.data.fd = fd;
	epoll_ctl(epfd , EPOLL_CTL_DEL , fd , &event);
}

int establish_tcp_connection(struct FtpConnect* client)
{
	if (client->dataIp[0]) 
	{
		client->dataSocket = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in servaddr;
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(client->dataPort);
		if (inet_aton(client->dataIp, &(servaddr.sin_addr)) <= 0) 
		{
			DebugInfo("error in port command");
			return -1;
		}
		if (connect(client->dataSocket, (struct sockaddr *) &servaddr,sizeof(struct sockaddr)) == -1) 
		{
			DebugInfo("connect出错！, the error is %d\n" , errno);
			return -1;
		}
		DebugInfo("data tcp, port connect success.\n");
		
		return 1;
	}
	else if (client->data_ServerSocket > 0) 
	{
		socklen_t sock = sizeof(struct sockaddr);
		struct sockaddr_in data_client;
		client->dataSocket = accept(client->data_ServerSocket,
				(struct sockaddr*) &data_client, &sock);

		if (client->dataSocket < 0) 
		{
			perror("accept error");
			return -1;
		} 
		else
		{
			socklen_t sock_length = sizeof(struct sockaddr);
			char client_ip[100];
			getpeername(client->dataSocket, (struct sockaddr*) &data_client,&sock_length);
			inet_ntop(AF_INET, &(data_client.sin_addr), client_ip,INET_ADDRSTRLEN);
			DebugInfo("data tcp ,%s connect to the host." , client_ip);
		}

		return 1;

	}
	return -1;

}


void cancel_tcp_connection(struct FtpConnect* client) 
{

	if (client->data_ServerSocket > 0) 
	{
		close(client->data_ServerSocket);
		client->data_ServerSocket = -1;
	}
	if (client->dataSocket > 0)
	{
		close(client->dataSocket);
		client->dataSocket = -1;
	}
	if (client->dataIp[0])
	{
		client->dataIp[0] = 0;
		client->dataPort = 0;
	}
}

void m_sleep(unsigned int secs)
{
  struct timeval time;
  time.tv_sec=secs/1000;
  time.tv_usec=(secs*1000)%1000000;
  select(0,NULL,NULL,NULL,&time);
}

void CmdTest(FtpConnect * , void *)
{
	DebugInfo("cmd Test is ok\n");
}


void HandlePwd(FtpConnect * con, void *data)
{

	DebugInfo("pwd cmd is running ,the path is %s\n", con->path);
	
    send(con->remoteSocket, con->path, strlen(con->path), 0);
}


void HandleCwd(FtpConnect * con, void *data)
{
	char buff[100];
    DIR* dp;
	string *path = (string *)data;

    dp = opendir((*path).c_str());

	if(dp == NULL)
	{
		sprintf(buff, "550 : No such file or directory.\r\n");
	}
	else
	{
		sprintf(buff, "250 CWD command successful.\r\n");
		strcpy(con->path , (*path).c_str());
	}

	DebugInfo("cwd is running, the data path is %s\n" , (*path).c_str());
	
    send(con->remoteSocket, buff, strlen(buff), 0);

}

void HandlePort(FtpConnect * con, void *data)
{
	if (con->dataSocket > 0) {
		close(con->dataSocket);
	}
	string *ip_prot = (string *)data;

	DebugInfo("Prot's data is :%s\n" , (*ip_prot).c_str());
	con->dataIp[0] = 0;
	int a, b, c, d, e;
	sscanf((*ip_prot).c_str(), "%d.%d.%d.%d:%d", &a, &b, &c, &d, &e);
	sprintf(con->dataIp, "%d.%d.%d.%d", a, b, c, d);
	con->dataPort = e;

	DebugInfo("Port is running , the data ip is :%s , the data port is :%d\n" , con->dataIp , con->dataPort);

	char connect[] = "200 PORT command successful.\r\n";
	send(con->remoteSocket, connect , strlen(connect) , 0);
}


void HandleList(FtpConnect * con, void *data)
{

	if (establish_tcp_connection(con) > 0) 
	{
		DebugInfo("cmd List establish tcp socket\n");
	}
	else 
	{
		char msg[] = "425 TCP connection cannot be established.\r\n";
		send(con->remoteSocket, msg , strlen(msg) , 0);
		return;
	}

	char path[128] = {};
    DIR* dp;
    struct dirent *ep;

	strcpy(path , con->path);
    dp = opendir(path);

    if(NULL != dp)
    {
        while(ep = readdir(dp)) 
        {
            if(ep->d_name[0] != '.')
            {
                DebugInfo("the dir's name is :%s\n",ep->d_name);
                send(con->dataSocket, ep->d_name, strlen(ep->d_name),0);
				m_sleep(1);
            }
           
        }
        if(-1 == send(con->dataSocket, CMD_LIST_END, strlen(CMD_LIST_END),0))   //send end
        {
            DebugInfo("cmd List ,Write endstring error!\n");
        }
    }
    else
    {
        if(-1 == send(con->dataSocket, CMD_LIST_END, strlen(CMD_LIST_END),0))   //send end
        {
            DebugInfo("cmd List , Write endstring error!\n");
			closedir(dp);
			cancel_tcp_connection(con);
            return ;
        }
        DebugInfo("cmd List ,Can't open the directory!\n");
    }

    closedir(dp);
	cancel_tcp_connection(con);

}

void HandlePASV(FtpConnect * con, void *data)
{
	if (con->dataSocket > 0) 
	{
		close(con->dataSocket);
		con->dataSocket = -1;
	}
	if (con->data_ServerSocket > 0) 
	{
		close(con->data_ServerSocket);
	}

	string *s_prot = (string *)data;
	int i_port = 0;
	sscanf((*s_prot).c_str(), "%d", &i_port);
	DebugInfo("PASV's port is %d" , i_port);
	
	con->data_ServerSocket = socket(AF_INET, SOCK_STREAM, 0);
	if (con->data_ServerSocket < 0) 
	{
		DebugInfo("cmd PASV opening socket error\n");
		char msg[] = "426 pasv failure\r\n";
		send(con->remoteSocket, msg , strlen(msg) , 0);
		return;
	}
	
	struct sockaddr_in server;
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(con->serverIp);
	server.sin_port = htons(i_port);
	if (bind(con->data_ServerSocket, (struct sockaddr*) &server,sizeof(struct sockaddr)) < 0) 
	{
		DebugInfo("cmd PASV binding error\n");
		char msg[] = "426 pasv failure\r\n";
		send(con->remoteSocket, msg , strlen(msg) , 0);
		return;
	}
	if (listen(con->data_ServerSocket, 1) < 0)
	{
		DebugInfo("cmd PASV binding error\n");
		char msg[] = "426 pasv failure\r\n";
		send(con->remoteSocket, msg , strlen(msg) , 0);
		return;
	}
	struct sockaddr_in file_addr;
	socklen_t file_sock_len = sizeof(struct sockaddr);
	getsockname(con->data_ServerSocket, (struct sockaddr*) &file_addr,&file_sock_len);
	int port = ntohs(file_addr.sin_port);
	DebugInfo("PASV is running , the data Server Ip is :%s , the prot is %d\n" , con->serverIp ,port);
	
}

void HandleSTOR(FtpConnect * con, void *data)
{
	FILE* file = NULL;
	char _path[400] = {};
	string *file_string = (string *)data;
	char fileName[40] = {};
	strcpy(fileName , (*file_string).c_str());
	strcpy(_path, con->path);
	if (_path[strlen(_path) - 1] != '/') {
		strcat(_path, "/");
	}
	strcat(_path, fileName);

	file = fopen(_path, "wb");
	DebugInfo("retr the file is %s\n" , _path);
	
	if (file == NULL ) 
	{
		DebugInfo("Open File error ,the errno is %d\n" , errno);
		char msg[] = "451 trouble to stor file\r\n";
		send(con->remoteSocket, msg , strlen(msg) , 0);
		return;
	}

	if (establish_tcp_connection(con) > 0) 
	{
		char msg_start[] = "150 Data connection accepted; transfer starting.\r\n";
		send(con->remoteSocket, msg_start ,strlen(msg_start) , 0);
		char buf[1000];
		int j = 0;
		while (1) 
		{
			j = recv(con->dataSocket, buf, 1000, 0);
			if (j == 0) 
			{
				cancel_tcp_connection(con);
				break;
			}
			if (j < 0) 
			{
				char msg_broken[] = "426 TCP connection was established but then broken\r\n";
				send(con->remoteSocket, msg_broken , strlen(msg_broken) , 0);
				return;
			}
			fwrite(buf, 1, j, file);

		}
		cancel_tcp_connection(con);
		fclose(file);
		
		char msg_stor_ok[] = "226 stor ok.\r\n";
		send(con->remoteSocket, msg_stor_ok ,strlen(msg_stor_ok) , 0);
	} 
	else 
	{
		char msg[] = "425 TCP connection cannot be established.\r\n";
		send(con->remoteSocket, msg , strlen(msg) , 0);
	}
}

void HandleRETR(FtpConnect * con, void *data)
{
	FILE* file = NULL;
	char _path[400] = {};
	string *file_string = (string *)data;
	char fileName[40] = {};
	strcpy(fileName , (*file_string).c_str());
	strcpy(_path, con->path);
	if (_path[strlen(_path) - 1] != '/') {
		strcat(_path, "/");
	}
	strcat(_path, fileName);

	
	file = fopen(_path, "rb");
	DebugInfo("retr the file is %s\n" , _path);

	
	if (file == NULL ) 
	{
		char msg[] = "451 trouble to retr file\r\n";
		send(con->remoteSocket, msg , strlen(msg) , 0);
		return;
	}

	if (establish_tcp_connection(con) > 0) 
	{
		char msg_start[] = "150 Data connection accepted; transfer starting.\r\n";
		send(con->remoteSocket, msg_start ,strlen(msg_start) , 0);
		char buf[1000];
		while (!feof(file)) 
		{
			int n = fread(buf, 1, 1000, file);
			int j = 0;
			while (j < n) 
			{
				j += send(con->dataSocket, buf + j, n - j, 0);
				DebugInfo("retr send %d data\n" , j);
				m_sleep(1);
			}
			DebugInfo("retr read file data is %s\n" , buf);
		}
		cancel_tcp_connection(con);
		fclose(file);
		
		char msg_stor_ok[] = "226 Transfer ok.\r\n";
		send(con->remoteSocket, msg_stor_ok ,strlen(msg_stor_ok) , 0);
	} 
	else 
	{
		char msg[] = "425 TCP connection cannot be established.\r\n";
		send(con->remoteSocket, msg , strlen(msg) , 0);
	}
}

