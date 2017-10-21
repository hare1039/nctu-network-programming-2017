#include <iostream>
#include <cstring>
#include <tuple>
#include <future>
#include <thread>
#include <mutex>              // std::mutex, std::unique_lock
#include <condition_variable> // std::condition_variable
#include <list>
#include <regex>
#include <map>

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
	std::string sender;
	std::string cmd;
	void clear()
	{
		cmd = sender = data = receiver = "";
	}

	std::string summery() const
	{
		return std::move("{\n  cmd:      " + cmd + ",\n"
		                 +  "  sender:   " + sender + ",\n"
		                 +  "  receiver: " + receiver + ",\n"
		                 +  "  data:     " + data + "}\n");
	}
};

struct Checklist
{
	std::map<std::string, bool> list;

	void show_all()
	{
		for(auto i: list)
			std::cout << i.first<< "\n";
	}
	
	bool contains(std::string s)
	{
		return list.find(s) != list.end();
	}
	bool inject_success(std::string s)
	{
		if (this->contains(s))
			return false;
		return list[s] = true;
	}
	bool remove_success(std::string s)
	{
		std::cout << "removing from list: " << s << "\n";
		if (not contains(s))
			return false;
		list.erase(s);
		return true;
	}
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
	Checklist               namelist;
	std::condition_variable message_cv;
	std::mutex              message_mtx;
	Message                 message;

	for(;;)
	{
		struct sockaddr_in client_addr;
        socklen_t    addrlen  = sizeof(client_addr);
        unsigned int clientfd = accept(socketfd, (struct sockaddr*)&client_addr, &addrlen);

        Chatter rat(clientfd, client_addr, addrlen);
        chatters.push_back(std::thread([&message_cv, &message, &message_mtx, &namelist, &rat] {
	        Chatter gopher = rat;

	        std::thread conn([&gopher, &message, &message_cv, &message_mtx, &namelist]{
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

						//std::cout << "RAW: [" << buf << "]\n";
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
						    {				    		
							    if(arguments == "anonymous")
							    {
								    std::string data("[Server] ERROR: Username cannot be anonymous\n");
								    size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
								    check_error("sending...", err);
								    break;
							    }
							    else if(arguments == gopher.name)
							    {

							    }
							    else if(namelist.contains(arguments))
							    {
								    std::string data("[Server] ERROR: " + arguments + " has been used by others\n");
								    size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
								    check_error("sending...", err);
								    break;
							    }
							    else if(arguments.size() < 2 || arguments.size() > 12 || not regex_match(arguments, std::regex("[A-Za-z]*")))
							    {
								    std::string data("[Server] ERROR: Username can only consists of 2~12 English letters\n");
								    size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
								    check_error("sending...", err);
								    break;
							    }

							    std::string old_name = gopher.name;
							    
							    if(namelist.inject_success(arguments))
								    gopher.name = arguments;
							    else
								    std::cout << "Change to same name??\n";
							    if(not namelist.remove_success(old_name))
								    std::cout << "name remove failed\n";

							    namelist.inject_success("anonymous");
							    
							    for(auto &name: namelist.list)
							    {
								    std::unique_lock<std::mutex> lock(message_mtx);
								    std::cout << "---------------------------- next msg to " +gopher.name+ "\n";
								    message.sender    = gopher.name;
								    message.receiver  = name.first;
								    message.data      = "[Server] " + ((name.first != gopher.name)?  old_name + " is": "You're") +
									                    " now known as " + arguments + ".\n";
								    message.cmd       = cmd;
								    std::cout << "\nnotifing " << name.first << " using msg:\n" + message.summery();
								    message_cv.notify_all();
							    }

						    	break;
						    }
					        case "tell"_hash:
					        {
						        auto pos         = arguments.find(" ");
						        check_error("tell argument parsing...", pos, [](auto pos){return pos == std::string::npos;});
						        std::unique_lock<std::mutex> lock(message_mtx);
						        message.sender   = gopher.name;
						        message.receiver = arguments.substr(0, pos);
						        message.data     = arguments.substr(pos + 1) + "\n";
						        message.cmd      = cmd;
						        if(gopher.name == "anonymous")
							    {
								    std::string data("[Server] ERROR: You are anonymous\n");
								    size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
								    check_error("sending...", err);
								    break;
							    }
						        
						        message_cv.notify_all();
							    break;
					        }

						case "yell"_hash:
							break;

						case "exit"_hash:
							break;

						default:
							std::cerr << "Unknown command: " << cmd << arguments << std::endl;
							break;
						}
					}
				}
				std::cout << std::this_thread::get_id() << " exited.\n";
	        }),



		    inter_msg([&gopher, &message, &message_cv, &message_mtx, &namelist]{
		        for(;;)
		        {
			        std::unique_lock<std::mutex> lock(message_mtx);
			        message_cv.wait(lock, [&](){
					        std::cout << gopher.name << " checked weather it needs continue by \n" + message.summery() + ((message.receiver == gopher.name)? "(true)\n": "(false)\n");
					        return (message.receiver == gopher.name);
				        });

			        switch(hash(message.cmd.c_str()))
			        {
			            case "who"_hash:
				            break;
			            case "name"_hash:
				        {
					        size_t err = send(gopher.clientfd, message.data.c_str(), message.data.size(), 0);
				            check_error("sending...", err);
				            break;
			            }
				            break;

			            case "tell"_hash:
			            {
				            std::string data = "[SERVER] " + message.sender + " tells you " + message.data;
				            size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
				            check_error("sending...", err);
				            break;
			            }
			            case "yell"_hash:
				            break;

			            case "exit"_hash:
				            break;

			            default:
				            break;
			        }
			        message.clear();
			        lock.unlock();
		        }
	        });
	        conn.join();
	        inter_msg.join();
		}));
	}
	for(auto &th : chatters)
		th.join();
    return 0;
}
