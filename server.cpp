#include <iostream>
#include <thread>

#if __cplusplus < 201103L
    #error("Please compile using -std=c++11 or higher")
#endif

extern "C"
{	
    #include <sys/socket.h>
    #include <unistd.h>
    #include <sys/types.h> 
    #include <netinet/in.h>
}

int main(int argc, *char[] argv)
{
	int socketfd = socket(AF_INET, SOCK_STREAM, 0); // error == -1

	// socket conn setup
	sockaddr_in socket_in;
	bzero(&socket_in, socket_in);
	socket_in.sin_family      = PF_INET;
	socket_in.sin_port        = htons(1234);
	socket_in.sin_addr.s_addr = inet_addr("127.0.0.1");
	
	int err = connect(socketfd, static_cast<sockaddr *>(&socket_in), sizeof(socket_in)); // err == -1
	char buf[100];
	for (int n; n = readline(sockfd, buf, sizeof(buf)) && n > 0;)
	{
	    buf[n] = '\0';
		std::cout << buf;
	}
	
	return 0;
}
