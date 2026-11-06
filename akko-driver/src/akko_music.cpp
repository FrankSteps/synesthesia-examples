// akko_music.cpp - Modo 20 (Music Patterns) do Akko MG75S sem TUI.
//
// Faz o mesmo que o TUI fazia no modo 20, usando só dois comandos:
//   0x07 SET_LEDPARAM  -> liga o modo 20 e define a cor
//   0x0D SET_AUDIO_VIZ -> 16 bandas (0-6), enviadas ~50x por segundo
//
// Compilar:  g++ -O2 -std=c++17 -Wall akko_music.cpp -o akko_music
// Usar:      ./akko_music FF0000                   (cor inicial RRGGBB)
//            ./akko_music FF0000 --fake            (bandas falsas, para testar sem áudio)
//            ./akko_music FF0000 /dev/hidraw3      (força o dispositivo)
// Trocar a cor com o programa rodando (de qualquer terminal ou do seu C++):
//            echo FF1800 > /tmp/akko-color
//
// Feche o iot_driver (TUI/serve) antes: dois processos no mesmo hidraw se atrapalham.


// bibliotecas
#include <iostream>             // lida com a entrada e saída do programa
#include <string>               // textos com std::string
#include <algorithm>            // algoritmos prontos, como limitar valores e achar o maior
#include <cmath>                // funções matemáticas (trigonométricas, raiz, logaritmo, potência...)
#include <csignal>              // tratar sinais do sistema, como o Ctrl+C
#include <cstdint>              // inteiros de tamanho fixo (uint8_t, int16_t...)
#include <cstring>              // copiar e comparar blocos de memória e strings no estilo C
#include <cctype>               // classificar caracteres (espaço, dígito hexadecimal)
#include <sys/stat.h>           // criar arquivos especiais (FIFO) e alterar permissões
#include <cerrno>               // códigos de erro do sistema (saber por que uma chamada falhou)
#include <cstdlib>              // utilidades gerais (variáveis de ambiente, conversão de texto em número)
#include <linux/hidraw.h>       // acesso direto a dispositivos HID no Linux (comunicação com o teclado)
#include <cstdio>               // entrada e saída no estilo C (mensagens de erro, popen...)
#include <sys/ioctl.h>          // enviar comandos de controle a dispositivos e pipes (ioctl)
#include <unistd.h>             // chamadas básicas do sistema POSIX (ler e fechar descritores)
#include <thread>               // controlar a execução da thread (pausas com sleep_for)
#include <chrono>               // medir e representar tempo (relógio e milissegundos)

// abrir arquivos e dispositivos do sistema (open e suas flags)
namespace fcn {
    #include <fcntl.h>
}


// Constantes e variáveis globais do programa:
//      FIFO_PATH -> arquivo especial por onde a cor é trocada com o programa rodando
//      FS        -> taxa de amostragem do áudio capturado (Hz)
//      N         -> amostras por bloco (44100 / 50 = 882, ou seja, 50 blocos por segundo)
//      g_run     -> flag que indica se o laço principal deve continuar rodando
static const char* FIFO_PATH = "/tmp/akko-color";
static const int   FS = 44100;
static const int   N  = 882;   // 44100 / 50 Hz
static volatile sig_atomic_t g_run = 1;


// Handler de sinal: ao receber Ctrl+C (SIGINT) ou SIGTERM, apenas desliga a flag
static void on_sig(int) {
    g_run = 0;
}


// HID

// Procura e abre o teclado Akko (vendor 0x3151, product 0x4010) em /dev/hidrawN.
// Se o usuário forçou um dispositivo, abre direto esse caminho.
// Retorna o descritor de arquivo, ou -1 se não encontrou.
static int open_keyboard(const char* forced) {
    if (forced) {
        return fcn::open(forced, O_RDWR);
    }

    for (int i = 0; i < 64; i++) {
        char path[32];
        snprintf(path, sizeof path, "/dev/hidraw%d", i);

        int fd = fcn::open(path, O_RDWR);
        if (fd < 0) {
            continue;
        }

        hidraw_devinfo di{};
        if (ioctl(fd, HIDIOCGRAWINFO, &di) == 0 && di.vendor == 0x3151 && di.product == 0x4010) {
            int sz = 0;
            if (ioctl(fd, HIDIOCGRDESCSIZE, &sz) == 0 && sz >= 5) {
                hidraw_report_descriptor rd{};
                rd.size = sz;

                // Interface de configuração: Usage Page 0xFFFF, Usage 0x02
                if (ioctl(fd, HIDIOCGRDESC, &rd) == 0 && memcmp(rd.value, "\x06\xff\xff\x09\x02", 5) == 0) {
                    fprintf(stderr, "teclado em %s\n", path);
                    return fd;
                }
            }
        }

        close(fd);
    }

    return EXIT_FAILURE;
}

// Envia um pacote de 64 bytes ao teclado como "feature report" (com report ID 0 na frente).
// Retorna true se o envio deu certo.
static bool send_feature(int fd, const uint8_t* p64) {
    uint8_t buf[65];
    buf[0] = 0;  // report ID 0
    memcpy(buf + 1, p64, 64);
    return ioctl(fd, HIDIOCSFEATURE(65), buf) >= 0;
}

// Comando 0x07 (SET_LEDPARAM): liga o modo 20 (Music Patterns) e define a cor RGB.
// O último byte é um checksum calculado sobre os 8 primeiros bytes.
// Um checksum garante a integridade das informações transmitidas.
static bool send_color(int fd, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t p[64] = {0};
    p[0] = 0x07;   // SET_LEDPARAM
    p[1] = 20;     // modo: Music Patterns
    p[2] = 0;      // velocidade (sem efeito no modo 20)
    p[3] = 4;      // brilho
    p[4] = 0x04;   // opção, igual ao que o TUI enviou
    p[5] = r;
    p[6] = g;
    p[7] = b;

    unsigned s = 0;
    for (int i = 0; i < 8; i++) {
        s += p[i];
    }
    p[8] = 255 - (s & 255);  // checksum "Bit8" e quivale a: p[8] = ~(s & 255);

    return send_feature(fd, p);
}

// Comando 0x0D (SET_AUDIO_VIZ): envia as 16 bandas de áudio (valores de 0 a 6)
// que o teclado usa para animar as luzes no modo 20.
static bool send_bands(int fd, const uint8_t bands[16]) {
    uint8_t p[64] = {0};
    p[0] = 0x0d;   // SET_AUDIO_VIZ
    p[7] = 0xf2;   // checksum (255 - 0x0d), fixo
    memcpy(p + 8, bands, 16);

    return send_feature(fd, p);
}

// Áudio -> 16 bandas (Goertzel)

// Analisador de áudio: transforma um bloco de amostras PCM em 16 níveis (0 a 6)
// usando o algoritmo de Goertzel (mede a energia de frequências específicas).
struct Analyzer {
    float coeff[16];          // coeficiente de Goertzel de cada banda
    float win[N];             // janela de Hann aplicada às amostras
    float smooth[16] = {0};   // nível suavizado de cada banda (evita tremulação)
    float gain_db = 0.f;      // ganho extra em dB (variável de ambiente AKKO_GAIN_DB)

    // Construtor: calcula os coeficientes das 16 bandas, a janela de Hann
    // e lê o ganho opcional da variável de ambiente AKKO_GAIN_DB.
    Analyzer() {
        for (int b = 0; b < 16; b++) {
            float f = 50.f * std::pow(12000.f / 50.f, b / 15.f);  // 50 Hz .. 12 kHz, escala log
            coeff[b] = 2.f * std::cos(2.f * (float)M_PI * f / FS);
        }

        for (int i = 0; i < N; i++) {
            win[i] = 0.5f - 0.5f * std::cos(2.f * (float)M_PI * i / (N - 1));
        }

        if (const char* g = getenv("AKKO_GAIN_DB")) {
            gain_db = (float)atof(g);
        }
    }

    // Processa um bloco de N amostras e preenche out[16] com os níveis de 0 a 6.
    // Para cada banda: aplica Goertzel, converte a magnitude para dB,
    // mapeia para 0..6 e suaviza (sobe rápido, desce devagar).
    void run(const int16_t* s, uint8_t out[16]) {
        for (int b = 0; b < 16; b++) {
            float s1 = 0;
            float s2 = 0;

            for (int i = 0; i < N; i++) {
                float x = s[i] / 32768.f * win[i];
                float s0 = x + coeff[b] * s1 - s2;
                s2 = s1;
                s1 = s0;
            }

            float power = s1 * s1 + s2 * s2 - coeff[b] * s1 * s2;
            float mag = std::sqrt(std::max(power, 0.f)) * 2.f / (N * 0.5f);
            float db = 20.f * std::log10(mag + 1e-9f) + gain_db;
            float lvl = std::clamp((db + 60.f) / 45.f * 6.f, 0.f, 6.f);  // -60..-15 dB -> 0..6

            if (lvl > smooth[b]) {
                smooth[b] = lvl;
            } else {
                smooth[b] = smooth[b] * 0.85f + lvl * 0.15f;
            }

            out[b] = (uint8_t)(smooth[b] + 0.5f);
        }
    }
};

// Lê exatamente n bytes do descritor fd (repete até completar).
// Retorna false se o áudio terminou ou deu erro.
static bool read_exact(int fd, void* dst, size_t n) {
    uint8_t* p = (uint8_t*)dst;

    while (n) {
        ssize_t r = read(fd, p, n);

        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r <= 0) {
            return false;
        }

        p += r;
        n -= (size_t)r;
    }

    return true;
}












// TESTER
// Gera 16 bandas falsas (ondas senoidais defasadas) para testar o teclado sem áudio (--fake).
static void fake_bands(double t, uint8_t out[16]) {
    for (int b = 0; b < 16; b++) {
        double v = 3.0 + 3.0 * std::sin(t * 3.0 + b * 0.5);
        out[b] = (uint8_t)std::clamp((int)(v + 0.5), 0, 6);
    }
}

// Pretendo brevemente recriar o efeito do fogo do DOOM neste espaço (mas não sei se é possível alterar as cores individualmente)









// Cor via FIFO

// Converte um texto hexadecimal (RRGGBB, com ou sem "#") em três bytes r, g, b.
// Retorna false se o texto for inválido.
static bool parse_hex_color(const char* s, uint8_t& r, uint8_t& g, uint8_t& b) {
    std::string t(s);

    while (!t.empty() && isspace((unsigned char)t.back())) {
        t.pop_back();
    }

    if (t.size() < 6) {
        return false;
    }

    t = t.substr(t.size() - 6);

    for (char c : t) {
        if (!isxdigit((unsigned char)c)) {
            return false;
        }
    }

    unsigned v = (unsigned)strtoul(t.c_str(), nullptr, 16);
    r = (v >> 16) & 255;
    g = (v >> 8) & 255;
    b = v & 255;

    return true;
}

// Função principal: lê os argumentos, abre o teclado, cria a FIFO de cor,
// inicia a captura de áudio (parec) e entra no laço que envia as bandas ~50x por segundo.
int main(int argc, char** argv) {
    uint8_t r = 255;
    uint8_t g = 255;
    uint8_t b = 255;
    bool fake = false;
    const char* dev = nullptr;

    // Interpreta os argumentos: "--fake", um caminho "/dev/..." ou uma cor RRGGBB
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--fake")) {
            fake = true;
        } else if (!strncmp(argv[i], "/dev/", 5)) {
            dev = argv[i];
        } else if (!parse_hex_color(argv[i], r, g, b)) {
            fprintf(stderr, "argumento inválido: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    int fd = open_keyboard(dev);
    if (fd < 0) {
        fprintf(stderr, "não foi possível abrir o teclado (tente sudo, ou passe /dev/hidrawN)\n");
        return EXIT_FAILURE;
    }

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);

    mkfifo(FIFO_PATH, 0666);
    chmod(FIFO_PATH, 0666);
    int ffd = fcn::open(FIFO_PATH, O_RDONLY | O_NONBLOCK);

    FILE* pa = nullptr;
    if (!fake) {
        const char* src = getenv("AKKO_PAREC_DEV");
        std::string cmd = std::string("parec --format=s16le --rate=44100 --channels=1 --latency-msec=20 -d ") + (src ? src : "@DEFAULT_MONITOR@") + " 2>/dev/null";
        pa = popen(cmd.c_str(), "r");

        if (!pa) {
            fprintf(stderr, "não foi possivel iniciar o parec (apt install pulseaudio-utils)\n");
            return EXIT_FAILURE;
        }
    }

    if (!send_color(fd, r, g, b)) {
        perror("SET_LEDPARAM");
        return EXIT_FAILURE;
    }

    fprintf(stderr, "modo 20 ativo, cor %02X%02X%02X. Troque com: echo RRGGBB > %s\n", r, g, b, FIFO_PATH);

    int afd = -1;
    if (pa) {
        afd = fileno(pa);
    }

    Analyzer an;
    int16_t pcm[N];
    uint8_t bands[16];
    auto t0 = std::chrono::steady_clock::now();

    // Laço principal: a cada ~20 ms verifica se há cor nova, gera as 16 bandas
    // (falsas ou do áudio real) e envia ao teclado, até receber Ctrl+C ou dar erro.
    while (g_run) {
        // nova cor?
        if (ffd >= 0) {
            char buf[64];
            ssize_t n = read(ffd, buf, sizeof buf - 1);

            if (n > 0) {
                buf[n] = 0;
                uint8_t nr;
                uint8_t ng;
                uint8_t nb;

                if (parse_hex_color(buf, nr, ng, nb) && (nr != r || ng != g || nb != b)) {
                    r = nr;
                    g = ng;
                    b = nb;

                    if (!send_color(fd, r, g, b)) {
                        perror("SET_LEDPARAM");
                    }
                }
            }
        }

        // bandas
        if (fake) {
            double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fake_bands(t, bands);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } else {
            // lê direto do pipe (sem buffer do stdio); bloqueia ~20 ms e dita o ritmo
            if (!read_exact(afd, pcm, sizeof pcm)) {
                fprintf(stderr, "áudio terminou/sem fonte\n");
                break;
            }

            // se acumulou atraso, descarta os frames velhos e fica com o mais recente
            int avail = 0;
            while (ioctl(afd, FIONREAD, &avail) == 0 && avail >= (int)sizeof pcm) {
                if (!read_exact(afd, pcm, sizeof pcm)) {
                    break;
                }
            }

            an.run(pcm, bands);
        }

        if (!send_bands(fd, bands)) {
            perror("SET_AUDIO_VIZ");
            break;
        }
    }

    // Encerramento: fecha o parec, a FIFO e o dispositivo HID
    if (pa) {
        pclose(pa);
    }
    if (ffd >= 0) {
        close(ffd);
    }
    close(fd);

    return EXIT_SUCCESS;
}