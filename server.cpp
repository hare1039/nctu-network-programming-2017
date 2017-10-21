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
#include <queue>
#include <random>

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

#define UNTIL(x) while(!(x))

constexpr
long long int hash(const char* str, int h = 0)
{
	return (!str[h] ? 5381 : (hash(str, h+1)*33) ^ str[h] );
}
constexpr
long long int operator "" _hash(char const* p, size_t)
{
	return hash(p);
}


inline void check_error(std::string && type, int err)
{
	if(err < 0)
	{
		std::cout << type << " failed. err: [" << err << "]. Reason: " << std::strerror(errno) << std::endl;
		//std::exit(1);
	}
}

inline void check_error(std::string && type, int err, std::function<bool(int)> &&pred)
{
	if(pred(err))
	{
		std::cout << type << " failed. err: [" << err << "]. Reason: " << std::strerror(errno) << std::endl;
		//std::exit(1);
	}
}

inline void check_error(std::string && type,
                        int err,
                        std::function<bool(int)> &&pred,
                        std::function<void(void)> &&at_error)
{
	if(pred(err))
	{
		std::cout << type << " failed. err: [" << err << "]. Reason: " << std::strerror(errno) << std::endl;
		at_error();
	}
}

struct Chatter
{
	int clientfd;
	struct sockaddr_in client_addr;
	int addrlen;
	bool need_exit = false;
	std::string name = "anonymous";
	Chatter(int fd, struct sockaddr_in in, int len):
		clientfd(fd),
		client_addr(in),
		addrlen(len){}

	Chatter(){} // empty
	bool is_anonymous()
	{
		return is_anonymous_name(name);
	}

	static
	bool is_anonymous_name(std::string target)
	{
		return target.size() > 15 /* len(ano...) + rand(10)*/ and target.find("anonymous") != std::string::npos;
	}
};

struct Message
{
	std::string data;
	std::string sender;
	std::string cmd;
	void clear()
	{
		cmd = sender = data = "";
	}

	std::string summery() const
	{
		return std::move("{\n  cmd:      " + cmd + ",\n"
		                 +  "  sender:   " + sender + ",\n"
		                 +  "  data:     " + data + "}\n");
	}
};

struct Mailbox
{
	std::map<std::string, std::queue<Message>> msgs;
	std::map<std::string, Chatter *>           infos;
	void show_all_users()
	{
		for(auto i: msgs)
			std::cout << i.first << " has " << i.second.size() << " messages.\n";
	}

	bool contains(std::string s)
	{
		return msgs.find(s) != msgs.end();
	}
	bool inject_and_succeed(std::string name, Chatter * rat)
	{
		if (this->contains(name))
			return false;
		msgs[name]  = std::queue<Message>();
		infos[name] = rat;
		return true;
	}
	bool remove_and_succeed(std::string name)
	{
		std::cout << "removing from user list: " << name << "\n";
		if (not contains(name))
			return false;
		msgs.erase(name);
		infos.erase(name);
		return true;
	}
};


int main(int argc, char *argv[])
{
	check_error("Argument reading", argc, [](int n){return n < 2;}, []{ std::cout << "Usage: ./server [port]\n";});
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
	std::mutex              mailbox_mtx;
	std::condition_variable mailbox_cv;
	Mailbox                 mailbox;

	std::cout << "Listening on :" << argv[1] << "\n";
	for(;;)
	{
		struct sockaddr_in client_addr;
        socklen_t    addrlen  = sizeof(client_addr);
        unsigned int clientfd = accept(socketfd, (struct sockaddr*)&client_addr, &addrlen);

        Chatter rat(clientfd, client_addr, addrlen);
        chatters.push_back(std::thread([&mailbox_cv, &mailbox, &mailbox_mtx, &rat] {
	        Chatter gopher = rat;
	        static auto& chrs = "0123456789"
		        "abcdefghijklmnopqrstuvwxyz"
		        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	        std::mt19937 rg{std::random_device{}()};
            std::uniform_int_distribution<std::string::size_type> pick(0, sizeof(chrs) - 2);

            do
            {
	            gopher.name = "anonymous";
	            for(int length(10); length; length--)
		            gopher.name += chrs[pick(rg)];
            } UNTIL(not mailbox.contains(gopher.name));

            // notify others
		    std::unique_lock<std::mutex> login_lock(mailbox_mtx);
            for(auto &name: mailbox.msgs)
            {
	            Message message;
	            message.sender    = gopher.name;
	            message.data      = "[Server] Someone is coming!\n";
	            message.cmd       = "directsend";
	            mailbox.msgs[name.first].push(message);
            }
            mailbox.inject_and_succeed(gopher.name, &gopher);
            login_lock.unlock();
            mailbox_cv.notify_all();


	        std::thread conn([&gopher, &mailbox, &mailbox_cv, &mailbox_mtx]{
		        std::string greet("[Server] Hello, anonymous!"//"[real: " + gopher.name + "]"
		                          " From: " + std::string(inet_ntoa(gopher.client_addr.sin_addr)) + ":" +
		                          std::to_string(ntohs(gopher.client_addr.sin_port)) + "\n");


		        size_t err = send(gopher.clientfd, greet.c_str(), greet.size(), 0);
		        check_error("sending...", err);
				char        buf[1000];
		        std::string buf_unlimited;
				for(;;)
				{
					int n = recv(gopher.clientfd, buf, sizeof(buf), 0);
					check_error("recving...", n, [](int n){return n == 0;}, [&]{
							// close connection
							std::unique_lock<std::mutex> lock(mailbox_mtx);
							if (not mailbox.remove_and_succeed(gopher.name))
								std::cout << "remove " << gopher.name << " failed\n";
							gopher.need_exit = true;
							std::cout << "\n---- showing mailboxes \n";
							mailbox.show_all_users();
							std::cout << "\n---- end\n";
							close(gopher.clientfd);
							for(auto &name: mailbox.msgs)
							{
							    Message message;
							    message.sender    = gopher.name;
							    message.data      = "[Server] " + (Chatter::is_anonymous_name(gopher.name)? "anonymous": gopher.name) +
								                    " is offline.\n";
							    message.cmd       = "directsend";
							    mailbox.msgs[name.first].push(message);
							}
						});
					if (n == 0)
						break;
					buf[n] = '\0';
					buf_unlimited += buf;
					auto pos = buf_unlimited.find("\n");

					if(pos != std::string::npos)
					{
						std::string space = buf_unlimited.substr(0,  pos);
						buf_unlimited     = buf_unlimited.substr(pos + 1);
						std::string cmd, arguments;
						if(std::regex_match(space, std::regex(".*\\s.*")))
						{
							auto pos  = space.find(" ");
							cmd       = space.substr(0, pos);
							arguments = space.substr(pos + 1);
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
    							std::string data;
    							std::unique_lock<std::mutex> lock(mailbox_mtx);
							    for(auto &name: mailbox.msgs)
							    {
								    auto &mice = mailbox.infos[name.first];
								    if(mice == nullptr)
									    std::cout << "null ptr warning:: name: " << name.first << "\n";
								    else
								        data+= "[Server] " + (Chatter::is_anonymous_name(name.first)? "anonymous": name.first) + " "
									        +  std::string(inet_ntoa(mice->client_addr.sin_addr)) + ":"
									        +  std::to_string(ntohs(mice->client_addr.sin_port)) +
									        ((name.first == gopher.name)? " ->me": "") + "\n";
							    }
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
							    else if(mailbox.contains(arguments))
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
//							    std::cout << "injecting " << arguments << "\n";

							    std::unique_lock<std::mutex> lock(mailbox_mtx);
							    if(mailbox.inject_and_succeed(arguments, &gopher))
								    gopher.name = arguments;
							    else
								    std::cout << "Change to same name??\n";

							    if(not mailbox.remove_and_succeed(old_name))
								    std::cout << old_name << " remove failed\n";
							    //mailbox.show_all_users();
								for(auto &name: mailbox.msgs)
							    {
								    Message message;
								    message.sender    = gopher.name;
								    message.data      = "[Server] " + ((name.first != gopher.name)? (Chatter::is_anonymous_name(old_name)? "anonymous": old_name) + " is": "You're") +
									                    " now known as " + arguments + ".\n";
								    message.cmd       = "directsend";
								    //std::cout << "\nnotifing " << name.first << " using msg:\n" + message.summery();
								    mailbox.msgs[name.first].push(message);
							    }
								lock.unlock();
								mailbox_cv.notify_all();

						    	break;
						    }
					        case "tell"_hash:
					        {
						        auto pos         = arguments.find(" ");
						        check_error("tell argument parsing...", pos, [](auto pos){return pos == std::string::npos;});
						        std::string targ = arguments.substr(0, pos);
						        if(Chatter::is_anonymous_name(gopher.name))
							    {
								    std::string data("[Server] ERROR: You are anonymous\n");
								    size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
								    check_error("sending...", err);
								    break;
							    }
						        else if(targ == "anonymous")
						        {
								    std::string data("[Server] ERROR: The client which you designated is anonymous.\n");
								    size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
								    check_error("sending...", err);
								    break;
						        }
						        else if(not mailbox.contains(targ))
						        {
								    std::string data("[Server] ERROR: The receiver doesn't exist.\n");
								    size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
								    check_error("sending...", err);
								    break;
						        }

						        Message message;
						        message.sender   = gopher.name;
						        message.data     = arguments.substr(pos + 1) + "\n";
						        message.cmd      = cmd;
						        std::unique_lock<std::mutex> lock(mailbox_mtx);
						        mailbox.msgs[targ].push(message);
						        lock.unlock();
						        mailbox_cv.notify_all();
						        std::string data("[Server] SUCCESS: Your message has been sent.\n");
						        size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
						        check_error("sending...", err);
							    break;
					        }
						    case "yell"_hash:
						    {
							    std::unique_lock<std::mutex> lock(mailbox_mtx);
							    for(auto &name: mailbox.msgs)
							    {
								    Message message;
								    message.sender    = gopher.name;
								    message.data      = "[Server] " + (Chatter::is_anonymous_name(gopher.name)? "anonymous": gopher.name) +
									                    " yells " + arguments + "\n";
								    message.cmd       = "directsend";
								    mailbox.msgs[name.first].push(message);
							    }
							    lock.unlock();
							    mailbox_cv.notify_all();
							    break;
						    }

						case "exit"_hash:
							break;

						default:
							std::string data("[Server] ERROR: Error command.\n");
							size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
							check_error("sending...", err);
							std::cerr << "Unknown command: " << cmd << arguments << std::endl;
							break;
						}
					}
				}
				std::cout << std::this_thread::get_id() << " exited.\n";
	        }),



		    inter_msg([&gopher, &mailbox, &mailbox_cv, &mailbox_mtx]{
				for(;;)
		        {
			        std::unique_lock<std::mutex> lock(mailbox_mtx);
			        std::cout << gopher.name << " is waiting for invoke...\n";
			        mailbox_cv.wait(lock, [&](){
					        std::cout << gopher.name << " checked weather it needs continue (" << std::boolalpha << not mailbox.msgs[gopher.name].empty() << ") (" << gopher.need_exit << ")\n";
					        return (not mailbox.msgs[gopher.name].empty()) || gopher.need_exit;
				        });
			        if(gopher.need_exit)
			        {
				        mailbox.remove_and_succeed(gopher.name);
				        break;
			        }

			        while(not mailbox.msgs[gopher.name].empty())
			        {
				        Message message = mailbox.msgs[gopher.name].front();
			            mailbox.msgs[gopher.name].pop();
			            std::cout << gopher.name << " has message: " << message.summery() << "\n";
			            switch(hash(message.cmd.c_str()))
			            {
			                case "tell"_hash:
			                {
				                std::string data = "[SERVER] " + message.sender + " tells you " + message.data;
				                size_t err = send(gopher.clientfd, data.c_str(), data.size(), 0);
				                check_error("sending...", err);
				                break;
			                }
			                case "directsend"_hash:
			                {
				                size_t err = send(gopher.clientfd, message.data.c_str(), message.data.size(), 0);
				                check_error("sending...", err);
			                }
			                default:
				                break;
			            }
			        }
			        mailbox_cv.notify_all();
		        }
				std::cout << std::this_thread::get_id() << " exited.\n";
	        });
	        conn.join();
	        inter_msg.join();
		}));
	}
	for(auto &th : chatters)
		th.join();
    return 0;
}
