#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include <curl/curl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

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
static std::string playlistUrl = "https://www.googleapis.com/youtube/v3/playlistItems";
static std::string part = "?part=id,snippet";
static std::string maxResults = "&maxResults=50";
static std::string authKey = "&key=GOOGLEAPI KEY";
static std::string search = "&q=";

void fatal(const std::string &err)
{
    std::cout << err << std::endl;
    exit(1);
}

bool ytc_strings_equal(const char *a, const char *b)
{
    bool result = (a == b);
    if(a && b)
    {
        while(*a && *b && *a == *b)
        {
            ++a;
            ++b;
        }

        result = ((*a == 0) && (*b == 0));
    }
    return result;
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

void ytc_pause()
{
    send(sockfd, "pause", 5, 0);
}

void ytc_stop()
{
    send(sockfd, "stop", 4, 0);
}

void parse_youtube_playlist(std::vector<std::string> &list, std::string playlistID, std::string pageToken)
{
    std::string query = playlistUrl + part + maxResults + authKey + "&playlistId=" + playlistID + "&pageToken=" + pageToken;
    curl_easy_setopt(curl, CURLOPT_URL, query.c_str());
    res = curl_easy_perform(curl);

    std::string NextPageToken = pageToken;
    std::stringstream ss(response);
    response = "";
    std::string curline;
    int id_cnt = 0, title_cnt = 0;

    while(std::getline(ss, curline))
    {
        if(curline.find("\"nextPageToken\":") != std::string::npos)
        {
            NextPageToken = ytc_split_string(curline)[3];
        }

        if (curline.find("\"title\":") != std::string::npos)
        {
            title_cnt += 1;
            list.push_back(curline);
        }

        if(curline.find("\"videoId\":") != std::string::npos)
        {
            if(id_cnt + 1 <= title_cnt)
            {
                id_cnt += 1;
                list.push_back(curline);
            }
        }
    }

    if(NextPageToken != pageToken)
    {
        parse_youtube_playlist(list, playlistID, NextPageToken);
    }
}

void ytc_add_from_youtube_playlist(int argc, char **argv)
{
    ytc_init_curl();    

    std::string playlistID = std::string(argv[2]);
    std::vector<yt_entry> videos;
    std::vector<std::string> list;
    parse_youtube_playlist(list, playlistID, "");

    for(int i = 0; i < list.size(); i+=2)
    {
        yt_entry entry;
        entry.title = ytc_split_string(list[i])[3];
        entry.url = youtubePrefix + ytc_split_string(list[i+1])[3];
        videos.push_back(entry);
    }

    std::string selected = "addlist";
    for(int i = 0; i < videos.size(); ++i)
    {
        selected += " " + videos[i].url + " " + videos[i].title + '\0';
    }
    send(sockfd, selected.c_str(), selected.size(), 0);

    curl_easy_cleanup(curl);
}

void ytc_add(int argc, char **argv)
{
    ytc_init_curl();

    if(argc < 3)
        fatal("Usage: ytc add <search> [-index <num>] (optional)");

    int index = 0, i;
    for(i = 2; i < argc; ++i)
    {
        if(ytc_strings_equal(argv[i], "-index"))
        {
            index = atoi(argv[i+1]);
            break;
        }

        search += argv[i];
        if(i < argc-1 && !ytc_strings_equal(argv[i+1], "-index"))
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

void ytc_shuffle()
{
    send(sockfd, "shuffle", 7, 0);
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

void ytc_video()
{
    send(sockfd, "video\n", 6, 0);
    std::string status = ytc_read_line(sockfd);
    std::cout << status << std::endl;
}

void ytc_screen(int argc, char **argv)
{
    if(argc == 3)
    {
        std::string command = "screen " + std::string(argv[2]) + "\n";
        send(sockfd, command.c_str(), command.size(), 0);
    }
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

void ytc_fullscreen()
{
    send(sockfd, "fullscreen\n", 11, 0);
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

void ytc_seekfw()
{
    send(sockfd, "seekfw", 6, 0);
}

void ytc_seekbk()
{
    send(sockfd, "seekbk", 6, 0);
}

void ytc_volup()
{
    send(sockfd, "volup", 5, 0);
}

void ytc_voldown()
{
    send(sockfd, "voldn", 5, 0);
}

void ytc_toggle_fav()
{
    send(sockfd, "fav\n", 4, 0);
    std::string status = ytc_read_line(sockfd);
    std::cout << status << std::endl;
}

void ytc_mark_fav()
{
    send(sockfd, "mkfav", 5, 0);
}

void ytc_unmark_fav(int argc, char **argv)
{
    std::string command = "rmfav";
    if(argc == 3)
        command += " " + std::string(argv[2]);
    send(sockfd, command.c_str(), command.size(), 0);
}

void ytc_list_fav()
{
    send(sockfd, "lsfav\n", 6, 0);
    std::string playlist = ytc_read_line(sockfd);
    std::cout << playlist << std::endl;
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
    if(ytc_strings_equal(argv[1], "play"))
        ytc_play(argc, argv);
    else if(ytc_strings_equal(argv[1], "pause"))
        ytc_pause();
    else if(ytc_strings_equal(argv[1], "stop"))
        ytc_stop();
    else if(ytc_strings_equal(argv[1], "add"))
        ytc_add(argc, argv);
    else if(ytc_strings_equal(argv[1], "addlist"))
        ytc_add_from_youtube_playlist(argc, argv);
    else if(ytc_strings_equal(argv[1], "del"))
        ytc_del(argc, argv);
    else if(ytc_strings_equal(argv[1], "clear"))
        ytc_clear();
    else if(ytc_strings_equal(argv[1], "fav"))
        ytc_toggle_fav();
    else if(ytc_strings_equal(argv[1], "mkfav"))
        ytc_mark_fav();
    else if(ytc_strings_equal(argv[1], "rmfav"))
        ytc_unmark_fav(argc, argv);
    else if(ytc_strings_equal(argv[1], "lsfav"))
        ytc_list_fav();
    else if(ytc_strings_equal(argv[1], "seekfw"))
        ytc_seekfw();
    else if(ytc_strings_equal(argv[1], "seekbk"))
        ytc_seekbk();
    else if(ytc_strings_equal(argv[1], "volup"))
        ytc_volup();
    else if(ytc_strings_equal(argv[1], "voldown") || ytc_strings_equal(argv[1], "voldn"))
        ytc_voldown();
    else if(ytc_strings_equal(argv[1], "ls"))
        ytc_list();
    else if(ytc_strings_equal(argv[1], "current"))
        ytc_current();
    else if(ytc_strings_equal(argv[1], "video"))
        ytc_video();
    else if(ytc_strings_equal(argv[1], "next"))
        ytc_next();
    else if(ytc_strings_equal(argv[1], "prev"))
        ytc_prev();
    else if(ytc_strings_equal(argv[1], "repeat"))
        ytc_repeat();
    else if(ytc_strings_equal(argv[1], "screen"))
        ytc_screen(argc, argv);
    else if(ytc_strings_equal(argv[1], "fullscreen"))
        ytc_fullscreen();
    else if(ytc_strings_equal(argv[1], "load"))
        ytc_load(argc, argv);
    else if(ytc_strings_equal(argv[1], "save"))
        ytc_save(argc, argv);
    else if(ytc_strings_equal(argv[1], "swap"))
        ytc_swap(argc, argv);
    else if(ytc_strings_equal(argv[1], "shuffle"))
        ytc_shuffle();
}

void ytc_print_usage()
{
    std::cout << "Usage: ytc play [index] (optional)" << std::endl;
    std::cout << "       ytc pause" << std::endl;
    std::cout << "       ytc stop" << std::endl;
    std::cout << "       ytc prev" << std::endl;
    std::cout << "       ytc next" << std::endl;
    std::cout << "       ytc seekbk" << std::endl;
    std::cout << "       ytc seekfw" << std::endl;
    std::cout << "       ytc add <search> [-index <num>] (optional)" << std::endl;
    std::cout << "       ytc addlist <youtube list id>" << std::endl;
    std::cout << "       ytc del [index] (optional)" << std::endl;
    std::cout << "       ytc mkfav" << std::endl;
    std::cout << "       ytc rmfav <index>" << std::endl;
    std::cout << "       ytc volup" << std::endl;
    std::cout << "       ytc voldown|voldn" << std::endl;
    std::cout << "       ytc current" << std::endl;
    std::cout << "       ytc load <playlist>" << std::endl;
    std::cout << "       ytc save <playlist>" << std::endl;
    std::cout << "       ytc shuffle" << std::endl;
    std::cout << "       ytc lsfav" << std::endl;
    std::cout << "       ytc ls" << std::endl;
    std::cout << "       ytc clear" << std::endl;
    std::cout << "       ytc swap <index> <index>" << std::endl;
    std::cout << "       ytc fav :toggles playback of favorites" << std::endl;
    std::cout << "       ytc repeat :toggles repeat-all" << std::endl;
    std::cout << "       ytc video :toggles video" << std::endl;
    std::cout << "       ytc fullscreen :toggles fullscreen video" << std::endl;
    std::cout << "       ytc screen <index>" << std::endl;
    std::cout << "       -host <hostname> (optional as the end)" << std::endl;
    exit(0);
}

void ytc_verify_arguments(int argc, char **argv)
{
    if(argc < 2)
        ytc_print_usage();

    if (!ytc_strings_equal(argv[1], "play") &&
        !ytc_strings_equal(argv[1], "pause") &&
        !ytc_strings_equal(argv[1], "stop") &&
        !ytc_strings_equal(argv[1], "prev") &&
        !ytc_strings_equal(argv[1], "next") &&
        !ytc_strings_equal(argv[1], "add") &&
        !ytc_strings_equal(argv[1], "addlist") &&
        !ytc_strings_equal(argv[1], "del") &&
        !ytc_strings_equal(argv[1], "fav") &&
        !ytc_strings_equal(argv[1], "mkfav") &&
        !ytc_strings_equal(argv[1], "rmfav") &&
        !ytc_strings_equal(argv[1], "lsfav") &&
        !ytc_strings_equal(argv[1], "current") &&
        !ytc_strings_equal(argv[1], "clear") &&
        !ytc_strings_equal(argv[1], "volup") &&
        !ytc_strings_equal(argv[1], "voldown") &&
        !ytc_strings_equal(argv[1], "voldn") &&
        !ytc_strings_equal(argv[1], "seekfw") &&
        !ytc_strings_equal(argv[1], "seekbk") &&
        !ytc_strings_equal(argv[1], "ls") &&
        !ytc_strings_equal(argv[1], "load") &&
        !ytc_strings_equal(argv[1], "save") &&
        !ytc_strings_equal(argv[1], "shuffle") &&
        !ytc_strings_equal(argv[1], "swap") &&
        !ytc_strings_equal(argv[1], "repeat") &&
        !ytc_strings_equal(argv[1], "video") &&
        !ytc_strings_equal(argv[1], "fullscreen") &&
        !ytc_strings_equal(argv[1], "screen"))
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
    if(ytc_strings_equal(argv[argc-2], "-host"))
        host = argv[argc-1];

    ytc_socket_setup(host);
    ytc_handle_command(argc, argv);
    shutdown(sockfd, 0);

    return 0;
}
