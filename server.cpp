#include <iostream>
#include <cstring>
#include <tuple>
#include <future>
#include <thread>
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable> // std::condition_variable
#include <list>
#include <regex>


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

constexpr 
unsigned int hash(const char* str, int h = 0)
{
    return !str[h] ? 5381 : (hash(str, h+1)*33) ^ str[h];
}
constexpr int operator "" _hash(char const* p, size_t)
{
	return hash(p);
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

struct Chatter
{
	int clientfd;
	struct sockaddr_in client_addr;
	int addrlen;

	std::string name = "anonymous";
	Chatter(int fd, struct sockaddr_in in, int len):
		clientfd(fd),
		client_addr(in),
		addrlen(len){}	
};

struct Message
{
	std::string data;
	std::string receiver;
};



int main(int argc, char *argv[])
{
	check_error("Argument reading", argc, [](int n){return n != 2;});
	int socketfd = socket(PF_INET, SOCK_STREAM, 0);
	check_error("Create socket", socketfd);
	// socket conn setup
	sockaddr_in socket_in;
	std::memset(&socket_in, 0, sizeof(sockaddr_in));	
	socket_in.sin_family      = AF_INET;
	socket_in.sin_port        = htons(std::stoi(argv[1]));
	socket_in.sin_addr.s_addr = INADDR_ANY;

	int err = bind(socketfd, (struct sockaddr *) &socket_in, sizeof(socket_in));
	check_error("binding...", err);
	listen(socketfd, 300);

	std::list<std::thread>  chatters;
	std::mutex              internel_conn_mtx;
	std::condition_variable internel_conn;


	for(;;)
	{
        struct sockaddr_in client_addr;
        socklen_t    addrlen  = sizeof(client_addr);
        unsigned int clientfd = accept(socketfd, (struct sockaddr*)&client_addr, &addrlen);

        Chatter rat(clientfd, client_addr, addrlen);
        chatters.push_back(std::thread([&internel_conn_mtx, &internel_conn, &rat] {
	        Message    message;
	        std::mutex message_mtx;
	        Chatter    gopher = rat;

	        auto comm_handler = [&gopher, &message]{
		        std::string greet("[Server] Hello, " + gopher.name + "!" +
		                          " From: " + inet_ntoa(gopher.client_addr.sin_addr) + ":" +
		                          std::to_string(ntohs(gopher.client_addr.sin_port)) + "\n");


		        size_t err = send(gopher.clientfd, greet.c_str(), greet.size(), 0);
		        check_error("sending...", err);
				char        buf[1000];
		        std::string buf_unlimited;
				for(;;)
				{
					int n = recv(gopher.clientfd, buf, sizeof(buf), 0);
					check_error("recving...", n, [](int n){return n == 0;});
					buf[n] = '\0';
					buf_unlimited += buf;
					auto pos = buf_unlimited.find("\n");
					if(pos != std::string::npos)
					{
						std::string space = buf_unlimited.substr(0,  pos);
						buf_unlimited     = buf_unlimited.substr(pos + 1);
						std::string cmd, arguments;
						std::cout << "parsing: [" << space << "]\n";
						if(std::regex_match(space, std::regex(".*\\s.*")))
						{
							auto pos  = space.find(" ");
							cmd       = space.substr(0, pos);
							arguments = space.substr(pos + 1);
							std::cout << "rcv cmd: [" << cmd << "], arg:[" << arguments << "]\n";
						}
						else
						{
							cmd = space;
						}


						switch(hash(cmd.c_str()))
						{
						case "who"_hash:
						{
							std::string data = "who?: you are " + gopher.name + "\n";
							size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
							check_error("sending...", err);
							break;
						}
						case "name"_hash:
							gopher.name = arguments;
							break;
    
						case "tell"_hash:
							break;
    
						case "yell"_hash:
							break;
    
						case "exit"_hash:
							break;
    
						default:
							std::cerr << "Unknown command: " << cmd << arguments << std::endl;
							break;
						}						
					}
					std::cout << buf;
				}
				std::cout << std::this_thread::get_id() << " exited.\n";
	        };

	        
	        auto internel_msg_handler = [&gopher, &message, &internel_conn_mtx, &internel_conn]{
		        std::unique_lock<std::mutex> lock(internel_conn_mtx);
		        internel_conn.wait(lock, [&](){return message.receiver == gopher.name;});		        
	        };


	        std::thread conn(comm_handler), inter_msg(internel_msg_handler);
	        conn.join();
	        inter_msg.join();
		})); 
	}
	for(auto &th : chatters)
		th.join();
    return 0;
}
