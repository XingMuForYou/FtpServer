#include <stdio.h>
#include <stdlib.h>
#include "FtpServer.h"


int main(int argc , char **argv)
{
	FtpServer *pServer = (FtpServer *)malloc(sizeof(FtpServer));

	if(argc == 2)
	{
		pServer->port = atoi(argv[1]);
		DebugInfo("Server port is %d\n", pServer->port);
	}
	else
	{
		DebugInfo("argument is only one\n");
		exit(1);
	}

	init_FtpServer(pServer);

	start_FtpServer(pServer);

	return 0;
}
