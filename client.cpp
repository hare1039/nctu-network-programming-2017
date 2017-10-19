#include <iostream>
#include <thread>
#include <cstring>
#include <tuple>

#if __cplusplus < 201103L
    #error("Please compile using -std=c++11 or higher")
#endif

extern "C"
{	
    #include <sys/socket.h>
    #include <unistd.h>
    #include <sys/types.h> 
    #include <netinet/in.h>
    #include <arpa/inet.h>
}

inline void check_error(std::string && type, int err)
{
	if(err < 0)
	{
		std::cout << type << " failed. err = " << err << ". errno: " << std::strerror(errno) << std::endl;
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	int socketfd = socket(AF_INET, SOCK_STREAM, 0);
	check_error("Create socket", socketfd);
	// socket conn setup
	sockaddr_in socket_in;
	std::memset(&socket_in, 0, sizeof(sockaddr_in));	
	socket_in.sin_family      = PF_INET;
	socket_in.sin_port        = htons(1234);
	socket_in.sin_addr.s_addr = inet_addr("127.0.0.1");
	
	int err = connect(socketfd, (sockaddr *)&socket_in, sizeof(socket_in));
	check_error("connect", err);
	
	for (auto [n, buf] = std::tuple<int, char[100]>{}; (n = read(socketfd, buf, sizeof(buf))) && n > 0;)
	{
	    buf[n] = '\0';
		std::cout << buf;
	}
	
	return 0;
}
