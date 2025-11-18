# Central de Auditoria e Simulação Wi-Fi com ESP32 🚦

> Sistema completo para ensino, pesquisa e automação de segurança de redes Wi-Fi usando ESP32 e interface web, com notificações via Telegram.

## Índice

- [Funcionalidades](#funcionalidades)
- [Tecnologias e Bibliotecas](#tecnologias-e-bibliotecas)
- [Como começar](#como-começar)
- [Observações de Uso](#observações-de-uso)
- [Estrutura do Projeto](#estrutura-do-projeto)
- [Segurança & Privacidade](#segurança--privacidade)
- [Galeria](#galeria)
- [Licença](#licença)
- [Créditos](#créditos)
- [Aviso legal](#aviso-legal)

---

## ⚡ Funcionalidades

- **Dashboard Responsivo:**  
  Visualização em tempo real das redes Wi-Fi detectadas, clientes conectados ao AP, presença de dispositivos e senhas coletadas.

- **Auditoria Wi-Fi:**  
  Varredura automática de até 50 redes próximas, com alerta para SSIDs duplicados (Evil Twin).

- **Portal Falso:**  
  Simulação de login que coleta tentativas de senha para fins educativos.

- **Brute Force Educacional:**  
  Teste de força bruta com dicionário fixo e dinâmico, sempre em redes de teste.  
  Resultados exibidos e enviados ao Telegram.

- **Monitoramento de Presença:**  
  Lista dinâmica de dispositivos relevantes (ex.: admins) online ou offline por MAC.

- **Exportação / Relatórios:**  
  Geração de arquivos TXT/CSV para uso externo e integração por API REST.

- **Notificações Telegram:**  
  Eventos críticos comunicados por bot dedicado.

- **WiFiManager e Reset de Configuração:**  
  Interface protegida para configurar ou resetar Wi-Fi.

- **Autenticação Protegida:**  
  Todas as áreas sensíveis requerem login admin.

---

## 🛠 Tecnologias e Bibliotecas

- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [UniversalTelegramBot](https://github.com/witnessmenow/UniversalTelegramBot)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- ESP32 APIs (`WiFi.h`, `WebServer.h`, `esp_wifi.h`)

---

## 🚀 Como começar

### Pré-requisitos

- VSCode com [PlatformIO](https://platformio.org/) instalado  
- Placa ESP32 compatível

### Clonagem e Setup

```bash
git clone https://github.com/seuusuario/esp32-wifi-audit.git
cd esp32-wifi-audit

Abra a pasta no VSCode, edite o platformio.ini conforme o modelo do seu ESP32 e ajuste o main.cpp para personalizar dispositivos/admins.

Upload para o ESP32
pio run --target upload

Acesso à WebApp

Acesse no navegador:

http://192.168.4.1/


Login padrão:

Usuário: admin  
Senha: admin123

🔎 Observações de Uso

O brute force e a coleta de senhas são somente para redes de teste e fins educacionais.

Nunca execute auditorias ou ataques em redes sem autorização formal.

O bot do Telegram exige configuração de token e chat_id no código.

A exportação permite gerar relatórios TXT/CSV para análise externa.

🗂 Estrutura do Projeto
/src/
  main.cpp
/platformio.ini
/data/          # Arquivos da WebApp (SPIFFS/LittleFS)
/lib/           # Bibliotecas locais (opcional)
/README.md

🔐 Segurança & Privacidade

Login obrigatório para rotas sensíveis.

Dados só são armazenados mediante exportação (por escolha do usuário).

Utilize sempre conforme as normas legais de segurança de redes.

🖼 Galeria

Insira aqui imagens das principais telas:

Tela de Login

Dashboard

Portal Falso

Brute Force

Monitoramento de Presença

Exportação

Notificações Telegram

📄 Licença

Escolha uma: MIT, GPL ou outra licença aberta.

✨ Créditos

Desenvolvido por André Coelho, João Victor Garmatz e Paulo Eduardo (2025) para ensino, pesquisa e compliance.
Agradecimentos a todos os criadores das bibliotecas utilizadas.

⚠️ Aviso legal

Este projeto é exclusivamente para uso educacional e ético.
Auditorias de redes reais sem permissão são consideradas crime em muitos países.
Use com responsabilidade!