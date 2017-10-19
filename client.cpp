#include <iostream>
#include <cstring>
#include <tuple>
#include <future>

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
		std::cout << type << " failed. err: [" << err << "]. Reason: " << std::strerror(errno) << std::endl;
		std::exit(1);
	}
}

inline void check_error(std::string && type, int err, std::function<bool(int)> f)
{
	if(f(err))
	{
		std::cout << type << " failed. err: [" << err << "]. Reason: " << std::strerror(errno) << std::endl;
		std::exit(1);
	}
}




int main(int argc, char *argv[])
{
	check_error("Argument reading", argc, [](int n){return n != 3;});
	int socketfd = socket(AF_INET, SOCK_STREAM, 0);
	check_error("Create socket", socketfd);
	// socket conn setup
	sockaddr_in socket_in;
	std::memset(&socket_in, 0, sizeof(sockaddr_in));	
	socket_in.sin_family      = PF_INET;
	socket_in.sin_port        = htons(std::stoi(argv[2]));
	socket_in.sin_addr.s_addr = inet_addr(argv[1]);
	
	int err = connect(socketfd, (sockaddr *)&socket_in, sizeof(socket_in));
	check_error("connection to ...", err);
	auto receiver = std::async(std::launch::async, [&socketfd](){
			char buf[1000];
			for(;;)
			{
				int n = recv(socketfd, buf, sizeof(buf), 0);
				check_error("recving...", n, [](int n){return n == 0;});
				buf[n] = '\0';
				if(std::string(buf) == "exit\n")
					break;
				std::cout << buf;
			}
			std::exit(0);
		}); 
	for (;;)
	{
		std::string command;
		std::getline(std::cin, command);
		command.append("\n");
		size_t err = send(socketfd, command.c_str(), command.size(), 0);
		check_error("sending...", err);
	}
	close(socketfd);
	
	return 0;
}
