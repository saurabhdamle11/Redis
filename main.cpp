#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <vector>
#include <sstream>
#include <functional>
#include <unordered_map>

using Args = std::vector<std::string>;
using CommandHandler = std::function<std::string(const Args&)>;

std::unordered_map<std::string,std::string>nakasha;
std::mutex mtx;

std::vector<std::string> parse_resp(const std::string&raw){
  std::vector<std::string>tokens;
  std::istringstream stream(raw);
  std::string line;

  std::getline(stream,line);
  if(line.empty() || line[0]!='*'){return tokens;}

  int num_elements = std::stoi(line.substr(1));

  for(int i=0;i<num_elements;++i){
    std::getline(stream,line);
    if(line.empty() || line[0]!='$'){break;}

    int len = std::stoi(line.substr(1));
    std::getline(stream,line);
    if(!line.empty() && line.back() =='\r'){line.pop_back();}
    tokens.push_back(line);
  }
  return tokens;
}


std::string cmd_ping(const Args&) {
  return "+PONG\r\n";
}

std::string cmd_echo(const Args& tokens) {
  if (tokens.size() < 2) return "-ERR wrong number of arguments for ECHO\r\n";
  const std::string& arg = tokens[1];
  return "$" + std::to_string(arg.size()) + "\r\n" + arg + "\r\n";
}

std::string cmd_get(const Args& tokens){
  if(tokens.size()<2){ return "-ERR wrong number of arguments for GET, got less than 1 arguments\r\n";}
  std::lock_guard<std::mutex>lk(mtx);
  std::string key = tokens[1];
  if(nakasha.find(key)!=nakasha.end()){
    return "$" + std::to_string(nakasha[key].size()) + "\r\n" + nakasha[key] + "\r\n";
  }
  return "$-1\r\n";
}

std::string cmd_set(const Args&tokens){
  if(tokens.size()<3){ return "-ERR wrong number of arguments for SET, got less than 2 arguments\r\n";}
  std::lock_guard<std::mutex>lk(mtx);
  std::string key = tokens[1];
  std::string value = tokens[2]; 
  nakasha[key]=value;
  return "+OK\r\n";
}

const std::unordered_map<std::string, CommandHandler> command_table = {
  {"PING", cmd_ping},
  {"ECHO", cmd_echo},
  {"GET", cmd_get},
  {"SET", cmd_set}
};


void handle_client(int client_fd){
  char buffer[1024];
  while(true){
    memset(buffer,0,sizeof(buffer));
    int bytes_received = recv(client_fd,buffer,sizeof(buffer),0);
    if(bytes_received<=0){break;}
    std::cout<<"Incoming bytes recived: "<<buffer<<"\n";
    std::string raw(buffer,bytes_received);
    std::vector<std::string> tokens = parse_resp(raw);
    if(tokens.empty()){continue;}

    std::string command = tokens[0];
    for(char&c:command){c=toupper(c);}
    std::cout<<"The command is "<<command<<"\n";

    auto it = command_table.find(command);
    std::string response = (it != command_table.end())
      ? it->second(tokens)
      : "-ERR unknown command\r\n";
    send(client_fd,response.c_str(),response.size(),0);
  }
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  
  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }
  
  // Since the tester restarts your program quite often, setting SO_REUSEADDR
  // ensures that we don't run into 'Address already in use' errors
  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }
  
  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);
  
  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }
  
  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }
  
  struct sockaddr_in client_addr;
  int client_addr_len = sizeof(client_addr);
  std::cout << "Waiting for a client to connect...\n";

  // You can use print statements as follows for debugging, they'll be visible when running tests.
  std::cout << "Logs from your program will appear here!\n";

  // Uncomment the code below to pass the first stage
  // 
  // int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
  // std::cout << "Client connected\n";

  const char *response = "+PONG\r\n";


  //send(client_fd,response,strlen(response),0);
  
  while (true)
  {
    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
    if(client_fd<0){
      std::cerr<<"Accept failed\n";
      continue;
    }
    std::cout<<"Client connected\n";
    std::thread(handle_client,client_fd).detach();
  }

  close(server_fd);

  return 0;
}
