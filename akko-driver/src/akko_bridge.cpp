/*
    g++ src/experiment_1.cpp -o builds/logs -lcurl
*/

// bibliotecas
#include <iostream>
#include <cstdlib>
#include <csignal>
#include <string>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>


// apelido para não usar nlohmann toda vez que for usar a biblioteca nlohmann/json.hpp
using json = nlohmann::json;


// caminho ABSOLUTO: funciona de qualquer pasta (/ ou ~), não depende de onde o programa foi executado.
// O FIFO é criado pelo akko_music; este programa só escreve nele.
const char* AKKO_COLOR_FIFO = "/tmp/akko-color";


// Ctrl+C encerra o laço principal
static volatile sig_atomic_t running = 1;
void on_sigint(int) { running = 0; }


//
size_t write_callback(char* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
    output->append(contents, total_size);
    return total_size;
}


// busca o JSON atual do Synesthesia. Devolve false se a requisição ou o JSON falharem
bool fetch_current(json& out) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8080/");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);

    CURLcode result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        std::cerr << "HTTP request failed: " << curl_easy_strerror(result) << '\n';
        return false;
    }

    out = json::parse(response, nullptr, false);  // false = não lança exceção
    return out.is_object();
}


// "#FF00FF" -> "FF00FF"
std::string remove_hash(std::string color) {
    if (!color.empty() && color[0] == '#') color.erase(0, 1);
    return color;
}


// escreve a cor no FIFO sem travar: O_NONBLOCK falha na hora se o akko_music não estiver lendo,
// e sem O_CREAT nunca cria um arquivo comum no lugar do FIFO
bool send_color_to_akko(const std::string& color) {
    int fd = open(AKKO_COLOR_FIFO, O_WRONLY | O_NONBLOCK);
    if (fd < 0) return false;

    std::string line = color + "\n";
    ssize_t n = write(fd, line.c_str(), line.size());
    close(fd);

    return n == (ssize_t)line.size();
}


// função principal
int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    std::signal(SIGINT, on_sigint);

    std::string lastColor, lastTitle;     // vazio: a primeira cor lida também é enviada
    bool warned = false;       // evita repetir o aviso a cada segundo

    while (running) {
        json data;

        if (fetch_current(data) && data.contains("color") && data["color"].is_string()) {
            std::string color = remove_hash(data["color"].get<std::string>());
            std::string title = data.value("title", "");


            if (color.size() == 6 && title != lastTitle) {
                if (send_color_to_akko(color)) {
                    lastColor = color;
                    lastTitle = title;
                    warned = false;

                    // debug
                    std::cout << "Info: " << data["artist"] << " - " << data["album"] << " - " << data["title"] << " - #" << color << '\n';

                } else if (!warned) {
                    std::cerr << "akko_music não está rodando (ou " << AKKO_COLOR_FIFO << " não existe)\n";
                    warned = true;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    curl_global_cleanup();
    return EXIT_SUCCESS;
}