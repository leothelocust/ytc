#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <curl/curl.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <cstring>
#include <cstdlib>

#define YTD_PORT 2940

struct yt_entry
{
    std::string title;
    std::string url;
};

static int sockfd;
static CURL *curl;
static CURLcode res;
static std::string response;

static std::string youtubePrefix = "http://youtube.com/watch?v=";
static std::string url = "https://www.googleapis.com/youtube/v3/search";
static std::string part = "?part=id,snippet";
static std::string maxResults = "&maxResults=50";
static std::string authKey = "&key=GOOGLEAPI KEY";
static std::string search = "&q=";

void fatal(const std::string &err)
{
    std::cout << err << std::endl;
    exit(1);
}

std::size_t write_to_string(char *ptr, std::size_t size, std::size_t count, std::string *stream)
{
    stream->append(ptr, 0, size*count);
    return size*count;
}

void ytc_init_curl()
{
    curl = curl_easy_init();
    if(!curl)
        fatal("Could not init curl!");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
}

std::string ytc_read_line(int sockfd)
{
    std::string message;
    char cur;
    while(recv(sockfd, &cur, 1, 0))
        message += cur;
    return message;
}

std::vector<std::string> ytc_split_string(const std::string &line)
{
    std::vector<std::string> elem;
    std::stringstream ss(line);
    std::string temp;
    while(std::getline(ss, temp, '"'))
        elem.push_back(temp);
    return elem;
}

void ytc_perform_search()
{
    std::string query = url + part + maxResults + authKey + search;
    curl_easy_setopt(curl, CURLOPT_URL, query.c_str());
    res = curl_easy_perform(curl);
}

std::vector<yt_entry> ytc_filter_results()
{
    std::vector<yt_entry> ytc_list;
    std::vector<std::string> list;
    std::stringstream ss(response);
    std::string curline;
    int id_cnt = 0, title_cnt = 0;
    while(std::getline(ss, curline))
    {
        if(curline.find("videoId") != std::string::npos)
        {
            id_cnt += 1;
            list.push_back(curline);
        }

        if (curline.find("title") != std::string::npos)
        {
            if(title_cnt + 1 <= id_cnt)
            {
                title_cnt += 1;
                list.push_back(curline);
            }
        }
    }

    for(int i = 0; i < list.size(); i+=2)
    {
        yt_entry entry = {ytc_split_string(list[i+1])[3], ytc_split_string(list[i])[3]};
        ytc_list.push_back(entry);
    }

    return ytc_list;
}

void ytc_play(int argc, char **argv)
{
    std::string command = "play";
    if(argc == 3)
        command += " " + std::string(argv[2]);
    send(sockfd, command.c_str(), command.size(), 0);
}

void ytc_stop()
{
    send(sockfd, "stop", 4, 0);
}

void ytc_add(int argc, char **argv)
{
    ytc_init_curl();

    if(argc < 3)
        fatal("Usage: ytc add <search> [-index <num>] (optional)");

    int index = 0, i;
    for(i = 2; i < argc; ++i)
    {
        if(strcmp(argv[i], "-index") == 0)
        {
            index = atoi(argv[i+1]);
            break;
        }

        search += argv[i];
        if(i < argc-2)
            search += "%20";
    }

    ytc_perform_search();
    std::vector<yt_entry> results = ytc_filter_results();

    if(index < 0 || index >= results.size())
        index = 0;

    std::string selected  = "add " + youtubePrefix + results[index].url + " " + results[index].title;
    send(sockfd, selected.c_str(), selected.size(), 0);

    curl_easy_cleanup(curl);
}

void ytc_del(int argc, char **argv)
{
    std::string command = "del";
    if(argc == 3)
        command += " " + std::string(argv[2]);
    send(sockfd, command.c_str(), command.size(), 0);
}

void ytc_clear()
{
    send(sockfd, "clear", 5, 0);
}

void ytc_list()
{
    send(sockfd, "list\n", 5, 0);
    std::string playlist = ytc_read_line(sockfd);
    std::cout << playlist << std::endl;
}

void ytc_current()
{
    send(sockfd, "current\n", 8, 0);
    std::string current = ytc_read_line(sockfd);
    std::cout << current << std::endl;
}

void ytc_visual()
{
    send(sockfd, "visual\n", 7, 0);
    std::string status = ytc_read_line(sockfd);
    std::cout << status << std::endl;
}

void ytc_next()
{
    send(sockfd, "next", 4, 0);
}

void ytc_prev()
{
    send(sockfd, "prev", 4, 0);
}

void ytc_repeat()
{
    send(sockfd, "repeat\n", 7, 0);
    std::string status = ytc_read_line(sockfd);
    std::cout << status << std::endl;
}

void ytc_load(int argc, char **argv)
{
    if(argc == 3)
    {
        std::string command = "load " + std::string(argv[2]) + "\n";
        send(sockfd, command.c_str(), command.size(), 0);
        std::string status = ytc_read_line(sockfd);
        std::cout << status << std::endl;
    }
}

void ytc_save(int argc, char **argv)
{
    if(argc == 3)
    {
        std::string command = "save " + std::string(argv[2]) + "\n";
        send(sockfd, command.c_str(), command.size(), 0);
        std::string status = ytc_read_line(sockfd);
        std::cout << status << std::endl;
    }
}

void ytc_swap(int argc, char **argv)
{
    if(argc == 4)
    {
        std::string command = "swap " + std::string(argv[2]) + " " + std::string(argv[3]);
        send(sockfd, command.c_str(), command.size(), 0);
    }
}

void ytc_handle_command(int argc, char **argv)
{
    if(strcmp(argv[1], "play") == 0)
        ytc_play(argc, argv);
    else if(strcmp(argv[1], "stop") == 0)
        ytc_stop();
    else if(strcmp(argv[1], "add") == 0)
        ytc_add(argc, argv);
    else if(strcmp(argv[1], "del") == 0)
        ytc_del(argc, argv);
    else if(strcmp(argv[1], "clear") == 0)
        ytc_clear();
    else if(strcmp(argv[1], "list") == 0 || strcmp(argv[1], "ls") == 0)
        ytc_list();
    else if(strcmp(argv[1], "current") == 0)
        ytc_current();
    else if(strcmp(argv[1], "visual") == 0)
        ytc_visual();
    else if(strcmp(argv[1], "next") == 0)
        ytc_next();
    else if(strcmp(argv[1], "prev") == 0)
        ytc_prev();
    else if(strcmp(argv[1], "repeat") == 0)
        ytc_repeat();
    else if(strcmp(argv[1], "load") == 0)
        ytc_load(argc, argv);
    else if(strcmp(argv[1], "save") == 0)
        ytc_save(argc, argv);
    else if(strcmp(argv[1], "swap") == 0)
        ytc_swap(argc, argv);
}

void ytc_print_usage()
{
    std::cout << "Usage: ytc play [index] (optional)" << std::endl;
    std::cout << "       ytc stop" << std::endl;
    std::cout << "       ytc next" << std::endl;
    std::cout << "       ytc prev" << std::endl;
    std::cout << "       ytc add <search> [-index <num>] (optional)" << std::endl;
    std::cout << "       ytc del [index] (optional)" << std::endl;
    std::cout << "       ytc load <playlist>" << std::endl;
    std::cout << "       ytc save <playlist>" << std::endl;
    std::cout << "       ytc list|ls" << std::endl;
    std::cout << "       ytc clear" << std::endl;
    std::cout << "       ytc swap <index> <index>" << std::endl;
    std::cout << "       ytc current" << std::endl;
    std::cout << "       ytc repeat :toggles repeat-all" << std::endl;
    std::cout << "       ytc visual :toggles video" << std::endl;
    std::cout << "       -host <hostname> (optional as the end)" << std::endl;
    exit(0);
}

void ytc_verify_arguments(int argc, char **argv)
{
    if(argc < 2)
        ytc_print_usage();

    if (strcmp(argv[1], "add") != 0 && 
        strcmp(argv[1], "play") != 0 && 
        strcmp(argv[1], "stop") != 0 && 
        strcmp(argv[1], "del") != 0 && 
        strcmp(argv[1], "current") != 0 &&
        strcmp(argv[1], "clear") != 0 && 
        strcmp(argv[1], "list") != 0 && 
        strcmp(argv[1], "ls") != 0 && 
        strcmp(argv[1], "next") != 0 && 
        strcmp(argv[1], "prev") != 0 && 
        strcmp(argv[1], "repeat") != 0 && 
        strcmp(argv[1], "visual") != 0 && 
        strcmp(argv[1], "load") != 0 && 
        strcmp(argv[1], "save") != 0 &&
        strcmp(argv[1], "swap") != 0)
            ytc_print_usage();
}

void ytc_socket_setup(std::string &host)
{
    struct sockaddr_in srv_addr;
    struct hostent *server;

    if((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1)
        fatal("Could not create socket!");

    server = gethostbyname(host.c_str());
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(YTD_PORT);
    std::memcpy(&srv_addr.sin_addr.s_addr, server->h_addr, server->h_length); 
    std::memset(&srv_addr.sin_zero, '\0', 8);

    if(connect(sockfd, (struct sockaddr*) &srv_addr, sizeof(struct sockaddr)) == -1)
        fatal("Connection failed! Make sure the YTD server is running...");
}

int main(int argc, char **argv)
{
    ytc_verify_arguments(argc, argv);

    std::string host = "localhost";
    if(strcmp(argv[argc-2], "-host") == 0)
        host = argv[argc-1];

    ytc_socket_setup(host);
    ytc_handle_command(argc, argv);
    shutdown(sockfd, 0);

    return 0;
}
