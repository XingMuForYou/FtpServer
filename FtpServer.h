#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <pthread.h>
#include <string>
#include <string.h>
#include <map>

#define DebugInfo(format,...) printf(format , ##__VA_ARGS__)
#define MaxListenCount   20
#define MaxEpollCount    128
#define BUFFER_SIZE      1000

#define CMD_LIST_END "LIST SEND END"

#define SERVER	 1
#define SCREENIN 2

using std::string;

typedef struct FtpServer{
	int  serverSocket;
	int  port;
	char _ip[20];
	int  epfd;
	struct sockaddr_in serverAddr;
}FtpServer;


typedef struct FtpConnect{
	char       serverIp[20];
	int 	   remoteSocket;
	pthread_t  thread_id;      //the thread id
	FtpServer* parentCon;
	char 	   dataIp[20];
	int		   dataPort;
	int 	   dataSocket;
	int 	   data_ServerSocket;
	char	   path[1024];
}FtpConnect;

typedef void(* CmdHandleFunction)(FtpConnect * , void *);

void addFdToEpoll(int epfd,int fd);

void delFdFromEpoll(int epfd,int fd);

void start_FtpServer(FtpServer *pServer);

void init_FtpServer(FtpServer *pServer);

void FtpServerHandleStdin(FtpServer *pServer);

void FtpServerHandleSocket(FtpServer *pServer);

void initFtpConnect(FtpConnect *pConnect , FtpServer *server , int remoteSocket);

void *Connect_Thread(void *data);

void WaitCmdToHandle(FtpConnect *connect);

void Recv_msg_Resolve(FtpConnect *con , char **buff , string *data);

int  establish_tcp_connection(struct FtpConnect* client);

void cancel_tcp_connection(struct FtpConnect* client);

void m_sleep(unsigned int secs);

void ExitFtpServer(FtpServer *pServer);

void CmdTest(FtpConnect * , void *);

void HandlePwd(FtpConnect * , void *);

void HandleCwd(FtpConnect * , void *);

void HandlePort(FtpConnect * , void *);

void HandleList(FtpConnect * , void *);

void HandlePASV(FtpConnect * con, void *data);

void HandleSTOR(FtpConnect * con, void *data);

void HandleRETR(FtpConnect * con, void *data);
