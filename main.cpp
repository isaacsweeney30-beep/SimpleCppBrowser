#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <sstream>
#include <thread>
#include <cstring>
#include <cctype>
#include <mutex>
#include <map>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

using namespace std;

map<string, map<string, string>> translations = {
    {"en", {
        {"title", "Simple C++ Browser"},
        {"ready", "Ready"},
        {"loading", "Loading..."},
        {"fetching", "Fetching page..."},
        {"back", "Back"},
        {"forward", "Forward"},
        {"browse", "Browse"},
        {"new_tab", "New Tab"},
        {"links", "Links"},
        {"summary", "Summary"},
        {"no_links", "No links found"},
        {"security_ok", "Secure browsing session"},
        {"security_warn", "Security warning: blocked risky content"},
        {"security_bad", "Security blocked: unsafe URL"},
        {"untitled", "Untitled page"},
        {"no_content", "No readable content found"},
        {"error_url_blocked", "URL blocked by security policy"},
        {"error_no_host", "Missing host"},
        {"error_socket", "Socket creation failed"},
        {"error_connection", "Connection failed"},
        {"error_ssl", "SSL connection failed"}
    }},
    {"es", {
        {"title", "Navegador Simple C++"},
        {"ready", "Listo"},
        {"loading", "Cargando..."},
        {"fetching", "Obteniendo página..."},
        {"back", "Atrás"},
        {"forward", "Adelante"},
        {"browse", "Navegar"},
        {"new_tab", "Nueva pestaña"},
        {"links", "Enlaces"},
        {"summary", "Resumen"},
        {"no_links", "No se encontraron enlaces"},
        {"security_ok", "Sesión de navegación segura"},
        {"security_warn", "Advertencia de seguridad: contenido riesgoso bloqueado"},
        {"security_bad", "Seguridad bloqueada: URL insegura"},
        {"untitled", "Página sin título"},
        {"no_content", "No hay contenido legible"},
        {"error_url_blocked", "URL bloqueada por política de seguridad"},
        {"error_no_host", "Host faltante"},
        {"error_socket", "Error al crear socket"},
        {"error_connection", "Conexión fallida"},
        {"error_ssl", "Error de conexión SSL"}
    }},
    {"fr", {
        {"title", "Navigateur Simple C++"},
        {"ready", "Prêt"},
        {"loading", "Chargement..."},
        {"fetching", "Récupération de la page..."},
        {"back", "Retour"},
        {"forward", "Avant"},
        {"browse", "Parcourir"},
        {"new_tab", "Nouvel onglet"},
        {"links", "Liens"},
        {"summary", "Résumé"},
        {"no_links", "Aucun lien trouvé"},
        {"security_ok", "Session de navigation sécurisée"},
        {"security_warn", "Avertissement de sécurité: contenu risqué bloqué"},
        {"security_bad", "Sécurité bloquée: URL non sécurisée"},
        {"untitled", "Page sans titre"},
        {"no_content", "Aucun contenu lisible trouvé"},
        {"error_url_blocked", "URL bloquée par la politique de sécurité"},
        {"error_no_host", "Hôte manquant"},
        {"error_socket", "Échec de la création du socket"},
        {"error_connection", "Connexion échouée"},
        {"error_ssl", "Erreur de connexion SSL"}
    }}
};

string g_currentLanguage = "en";

string t(const string& key) {
    if (translations[g_currentLanguage].count(key)) {
        return translations[g_currentLanguage][key];
    }
    return translations["en"][key];
}

struct UrlParts {
    string protocol;
    string host;
    string path;
    int port;
};

string url_decode(const string& input) {
    string output;
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            string hex = input.substr(i + 1, 2);
            try {
                int code = stoi(hex, nullptr, 16);
                output.push_back(static_cast<char>(code));
                i += 2;
            } catch (...) {
                output.push_back(input[i]);
            }
        } else if (input[i] == '+') {
            output.push_back(' ');
        } else {
            output.push_back(input[i]);
        }
    }
    return output;
}

string trim(const string& value) {
    size_t start = 0;
    while (start < value.size() && isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

string lower_string(const string& value) {
    string out = value;
    transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(tolower(ch));
    });
    return out;
}

bool is_dangerous_scheme(const string& url) {
    string lower = lower_string(trim(url));
    return lower.rfind("javascript:", 0) == 0 ||
           lower.rfind("file:", 0) == 0 ||
           lower.rfind("data:", 0) == 0 ||
           lower.rfind("vbscript:", 0) == 0;
}

bool is_valid_url(const string& url) {
    if (url.empty()) return false;
    if (is_dangerous_scheme(url)) return false;
    return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

string extract_title(const string& html) {
    smatch match;
    regex title_pattern("<title[^>]*>(.*?)</title>", regex::icase | regex::ECMAScript);
    if (regex_search(html, match, title_pattern)) {
        string title = match[1].str();
        title = regex_replace(title, regex("<[^>]+>"), " ");
        title = regex_replace(title, regex("\\s+"), " ");
        return trim(title);
    }
    return t("untitled");
}

string strip_tags(const string& html) {
    string text = regex_replace(html, regex("<script[^>]*>.*?</script>", regex::icase | regex::ECMAScript | regex::multiline), " ");
    text = regex_replace(text, regex("<style[^>]*>.*?</style>", regex::icase | regex::ECMAScript | regex::multiline), " ");
    text = regex_replace(text, regex("<[^>]+>"), " ");
    text = regex_replace(text, regex("&nbsp;"), " ");
    text = regex_replace(text, regex("&amp;"), "&");
    text = regex_replace(text, regex("&lt;"), "<");
    text = regex_replace(text, regex("&gt;"), ">");
    text = regex_replace(text, regex("&quot;"), "\"");
    text = regex_replace(text, regex("&#39;"), "'");
    text = regex_replace(text, regex("\\s+"), " ");
    return trim(text);
}

vector<string> extract_links(const string& html) {
    vector<string> links;
    regex link_pattern("href\\s*=\\s*['\"]?([^'\" >]+)", regex::icase | regex::ECMAScript);
    sregex_iterator it(html.begin(), html.end(), link_pattern);
    sregex_iterator end;
    for (; it != end; ++it) {
        string link = (*it)[1].str();
        if (link.empty() || link == "#") continue;
        if (is_dangerous_scheme(link)) continue;
        if (link.rfind("http://", 0) == 0 || link.rfind("https://", 0) == 0 || link.rfind("mailto:", 0) == 0) {
            links.push_back(link);
        } else if (link.rfind("/", 0) == 0) {
            links.push_back(link);
        } else {
            links.push_back(link);
        }
    }
    return links;
}

string escape_json(const string& input) {
    string out;
    out.reserve(input.size());
    for (char ch : input) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char temp[7];
                    snprintf(temp, sizeof(temp), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(ch)));
                    out += temp;
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

string build_json_response(const string& html) {
    string title = extract_title(html);
    string text = strip_tags(html);
    vector<string> links = extract_links(html);

    if (text.size() > 500) {
        text = text.substr(0, 500) + "...";
    }

    string limited_html = html;
    if (limited_html.size() > 250000) {
        limited_html = limited_html.substr(0, 250000);
    }

    string json = "{\n";
    json += "  \"title\": \"" + escape_json(title) + "\",\n";
    json += "  \"summary\": \"" + escape_json(text) + "\",\n";
    json += "  \"html\": \"" + escape_json(limited_html) + "\",\n";
    json += "  \"links\": [\n";
    for (size_t i = 0; i < links.size() && i < 10; ++i) {
        if (i > 0) json += ",\n";
        json += "    \"" + escape_json(links[i]) + "\"";
    }
    json += "\n  ]\n";
    json += "}\n";
    return json;
}

UrlParts parse_url(const string& input_url) {
    UrlParts parts;
    parts.protocol = "http";
    parts.port = 80;

    string url = trim(input_url);

    if (url.rfind("http://", 0) == 0) {
        parts.protocol = "http";
        parts.port = 80;
        url = url.substr(7);
    } else if (url.rfind("https://", 0) == 0) {
        parts.protocol = "https";
        parts.port = 443;
        url = url.substr(8);
    }

    size_t slash_pos = url.find('/');
    string host_part;
    if (slash_pos != string::npos) {
        host_part = url.substr(0, slash_pos);
        parts.path = url.substr(slash_pos);
    } else {
        host_part = url;
        parts.path = "/";
    }

    size_t colon_pos = host_part.find(':');
    if (colon_pos != string::npos) {
        parts.host = host_part.substr(0, colon_pos);
        parts.port = stoi(host_part.substr(colon_pos + 1));
    } else {
        parts.host = host_part;
    }

    return parts;
}

string http_response_body(const string& response) {
    size_t pos = response.find("\r\n\r\n");
    if (pos == string::npos) return response;
    return response.substr(pos + 4);
}

string fetch_url(const string& input_url) {
    if (!is_valid_url(input_url)) {
        return "<error>" + t("error_url_blocked") + "</error>";
    }

    UrlParts parts = parse_url(trim(input_url));
    if (parts.host.empty()) {
        return "<error>" + t("error_no_host") + "</error>";
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    string port_str = to_string(parts.port);
    addrinfo* res = nullptr;
    int status = getaddrinfo(parts.host.c_str(), port_str.c_str(), &hints, &res);
    if (status != 0) {
        return "<error>Failed to resolve host</error>";
    }

    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == -1) {
        freeaddrinfo(res);
        return "<error>" + t("error_socket") + "</error>";
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        close(sock);
        freeaddrinfo(res);
        return "<error>" + t("error_connection") + "</error>";
    }

    string request =
        "GET " + parts.path + " HTTP/1.1\r\n" +
        "Host: " + parts.host + "\r\n" +
        "User-Agent: SimpleCppBrowser/2.0\r\n" +
        "Accept: text/html,application/xhtml+xml,*/*\r\n" +
        "Connection: close\r\n\r\n";

    string response;

    if (parts.protocol == "https") {
        SSL_library_init();
        SSL_load_error_strings();

        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            close(sock);
            freeaddrinfo(res);
            return "<error>Failed to create SSL context</error>";
        }

        SSL* ssl = SSL_new(ctx);
        if (!ssl) {
            SSL_CTX_free(ctx);
            close(sock);
            freeaddrinfo(res);
            return "<error>Failed to create SSL object</error>";
        }

        SSL_set_fd(ssl, sock);
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sock);
            freeaddrinfo(res);
            return "<error>" + t("error_ssl") + "</error>";
        }

        if (SSL_write(ssl, request.c_str(), static_cast<int>(request.size())) <= 0) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(sock);
            freeaddrinfo(res);
            return "<error>SSL request failed</error>";
        }

        char buffer[4096];
        while (true) {
            int bytes = SSL_read(ssl, buffer, sizeof(buffer));
            if (bytes <= 0) break;
            response.append(buffer, bytes);
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(sock);
        freeaddrinfo(res);
    } else {
        send(sock, request.c_str(), request.size(), 0);
        char buffer[4096];
        while (true) {
            ssize_t bytes = recv(sock, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            response.append(buffer, bytes);
        }
        close(sock);
        freeaddrinfo(res);
    }

    return http_response_body(response);
}

string build_home_page() {
    return R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>Simple C++ Browser</title>
  <style>
    :root {
      --bg: #0f172a;
      --panel: #111827;
      --card: #1f2937;
      --card-alt: #0b1220;
      --accent: #3b82f6;
      --accent-2: #60a5fa;
      --text: #f9fafb;
      --muted: #d1d5db;
      --border: #374151;
      --success: #a7f3d0;
      --warning: #fbbf24;
      --danger: #f87171;
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: var(--bg);
      color: var(--text);
    }

    .container {
      max-width: 1400px;
      margin: 24px auto;
      padding: 16px;
    }

    .browser-shell {
      background: var(--panel);
      border: 1px solid var(--border);
      border-radius: 14px;
      overflow: hidden;
      box-shadow: 0 12px 30px rgba(0,0,0,0.25);
    }

    .header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 12px 16px;
      background: #0d1117;
      border-bottom: 1px solid var(--border);
    }

    .header-title {
      font-size: 18px;
      font-weight: bold;
    }

    .lang-selector {
      display: flex;
      gap: 8px;
    }

    .lang-btn {
      padding: 6px 12px;
      background: var(--card-alt);
      color: var(--text);
      border: 1px solid var(--border);
      border-radius: 6px;
      cursor: pointer;
      font-size: 12px;
    }

    .lang-btn.active {
      background: var(--accent);
      border-color: var(--accent);
    }

    .toolbar {
      display: flex;
      gap: 10px;
      padding: 12px;
      background: #182132;
      border-bottom: 1px solid var(--border);
      align-items: center;
      flex-wrap: wrap;
    }

    .nav-buttons {
      display: flex;
      gap: 8px;
    }

    button {
      background: var(--accent);
      color: white;
      border: none;
      border-radius: 8px;
      padding: 10px 14px;
      cursor: pointer;
      font-size: 0.95rem;
      font-weight: 500;
    }

    button:hover { background: #2563eb; }
    button:disabled {
      background: #6b7280;
      cursor: not-allowed;
      opacity: 0.7;
    }

    input {
      flex: 1;
      min-width: 220px;
      background: var(--card-alt);
      color: var(--text);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 10px 12px;
      font-size: 1rem;
    }

    input::placeholder {
      color: var(--muted);
    }

    .main-view {
      display: grid;
      grid-template-columns: 420px 1fr;
      gap: 16px;
      padding: 18px;
      min-height: 650px;
      background: var(--card);
    }

    .panel {
      background: rgba(255,255,255,0.02);
      border: 1px solid var(--border);
      border-radius: 10px;
      padding: 14px;
      min-height: 290px;
      overflow-y: auto;
    }

    .status {
      font-weight: bold;
      margin-bottom: 14px;
      padding: 8px 12px;
      border-radius: 6px;
    }

    .status.ok { 
      color: var(--success);
      background: rgba(167, 243, 208, 0.1);
    }
    .status.warn { 
      color: var(--warning);
      background: rgba(251, 191, 36, 0.1);
    }
    .status.bad { 
      color: var(--danger);
      background: rgba(248, 113, 113, 0.1);
    }

    .meta h2 {
      margin: 0 0 8px;
      font-size: 1.4rem;
    }

    .summary {
      background: rgba(255,255,255,0.03);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 14px;
      white-space: pre-wrap;
      line-height: 1.6;
      margin-bottom: 18px;
      min-height: 120px;
      max-height: 200px;
      overflow-y: auto;
    }

    .links {
      list-style: none;
      padding: 0;
      margin: 0;
    }

    .links li {
      margin-bottom: 8px;
      word-break: break-word;
    }

    .links a {
      color: var(--accent-2);
      text-decoration: none;
      font-size: 0.9rem;
    }

    .links a:hover {
      text-decoration: underline;
    }

    .render-panel {
      position: relative;
      overflow: hidden;
      background: #f3f4f6;
      border-radius: 10px;
      border: 1px solid var(--border);
      min-height: 560px;
    }

    #pageFrame {
      width: 100%;
      height: 100%;
      min-height: 560px;
      border: none;
      background: white;
    }

    h3 {
      margin: 12px 0 8px 0;
      font-size: 1rem;
    }

    @media (max-width: 1000px) {
      .main-view {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="browser-shell">
      <div class="header">
        <div class="header-title">Simple C++ Browser</div>
        <div class="lang-selector">
          <button class="lang-btn active" data-lang="en" onclick="changeLanguage('en')">EN</button>
          <button class="lang-btn" data-lang="es" onclick="changeLanguage('es')">ES</button>
          <button class="lang-btn" data-lang="fr" onclick="changeLanguage('fr')">FR</button>
        </div>
      </div>

      <div class="toolbar">
        <div class="nav-buttons">
          <button id="backBtn" type="button">← Back</button>
          <button id="forwardBtn" type="button">Forward →</button>
        </div>
        <input id="urlInput" type="text" value="https://example.com" placeholder="Enter an address" />
        <button id="fetchButton" type="button">Browse</button>
      </div>

      <div class="main-view">
        <div class="panel">
          <div id="status" class="status ok">Ready</div>
          <div class="meta">
            <h2 id="pageTitle">Page title</h2>
          </div>
          <div id="summary" class="summary">Loading page...</div>
          <h3>Links</h3>
          <ul id="links" class="links"></ul>
        </div>

        <div class="render-panel">
          <iframe id="pageFrame" title="Rendered page"></iframe>
        </div>
      </div>
    </div>
  </div>

  <script>
    let currentLanguage = 'en';
    
    const translations = {
      en: {
        ready: 'Ready',
        loading: 'Loading...',
        fetching: 'Fetching page...',
        back: 'Back',
        forward: 'Forward',
        browse: 'Browse',
        links: 'Links',
        no_links: 'No links found',
        security_ok: 'Secure browsing session',
        security_warn: 'Security warning: blocked risky content',
        security_bad: 'Security blocked: unsafe URL',
        untitled: 'Untitled page',
        no_content: 'No readable content found',
        request_failed: 'Request failed'
      },
      es: {
        ready: 'Listo',
        loading: 'Cargando...',
        fetching: 'Obteniendo página...',
        back: 'Atrás',
        forward: 'Adelante',
        browse: 'Navegar',
        links: 'Enlaces',
        no_links: 'No se encontraron enlaces',
        security_ok: 'Sesión de navegación segura',
        security_warn: 'Advertencia de seguridad: contenido riesgoso bloqueado',
        security_bad: 'Seguridad bloqueada: URL insegura',
        untitled: 'Página sin título',
        no_content: 'No hay contenido legible',
        request_failed: 'Error en la solicitud'
      },
      fr: {
        ready: 'Prêt',
        loading: 'Chargement...',
        fetching: 'Récupération de la page...',
        back: 'Retour',
        forward: 'Avant',
        browse: 'Parcourir',
        links: 'Liens',
        no_links: 'Aucun lien trouvé',
        security_ok: 'Session de navigation sécurisée',
        security_warn: 'Avertissement de sécurité: contenu risqué bloqué',
        security_bad: 'Sécurité bloquée: URL non sécurisée',
        untitled: 'Page sans titre',
        no_content: 'Aucun contenu lisible trouvé',
        request_failed: 'Erreur de requête'
      }
    };

    function t(key) {
      return translations[currentLanguage][key] || translations['en'][key];
    }

    function changeLanguage(lang) {
      currentLanguage = lang;
      document.querySelectorAll('.lang-btn').forEach(btn => {
        btn.classList.toggle('active', btn.dataset.lang === lang);
      });
      updateUI();
    }

    function updateUI() {
      document.getElementById('backBtn').textContent = '← ' + t('back');
      document.getElementById('forwardBtn').textContent = t('forward') + ' →';
      document.getElementById('fetchButton').textContent = t('browse');
      const linkHeader = document.querySelector('.main-view .panel h3');
      if (linkHeader) linkHeader.textContent = t('links');
    }

    let tabs = [{
      id: 1,
      title: t('untitled'),
      url: 'https://example.com',
      history: ['https://example.com'],
      historyIndex: 0,
      summary: t('loading'),
      links: [],
      html: '<html><body><p>Loading...</p></body></html>',
      securityStatus: 'ok'
    }];

    let activeTabId = 1;

    function getActiveTab() {
      return tabs.find(tab => tab.id === activeTabId) || tabs[0];
    }

    function renderPageContent() {
      const tab = getActiveTab();
      const titleEl = document.getElementById('pageTitle');
      const summaryEl = document.getElementById('summary');
      const linksEl = document.getElementById('links');
      const urlEl = document.getElementById('urlInput');
      const frameEl = document.getElementById('pageFrame');
      const statusEl = document.getElementById('status');

      titleEl.textContent = tab.title || t('untitled');
      summaryEl.textContent = tab.summary || t('no_content');
      urlEl.value = tab.url || '';
      frameEl.srcdoc = tab.html || '<html><body><p>Render preview unavailable.</p></body></html>';

      statusEl.className = 'status ' + (tab.securityStatus || 'ok');
      statusEl.textContent = tab.securityStatus === 'warn'
        ? t('security_warn')
        : tab.securityStatus === 'bad'
          ? t('security_bad')
          : t('security_ok');

      linksEl.innerHTML = '';
      if (Array.isArray(tab.links) && tab.links.length > 0) {
        tab.links.forEach(link => {
          const li = document.createElement('li');
          const a = document.createElement('a');
          a.href = link;
          a.target = '_blank';
          a.rel = 'noopener noreferrer';
          a.textContent = link;
          li.appendChild(a);
          linksEl.appendChild(li);
        });
      } else {
        const li = document.createElement('li');
        li.textContent = t('no_links');
        linksEl.appendChild(li);
      }

      updateNavButtons();
    }

    function updateNavButtons() {
      const tab = getActiveTab();
      document.getElementById('backBtn').disabled = tab.historyIndex <= 0;
      document.getElementById('forwardBtn').disabled = tab.historyIndex >= tab.history.length - 1;
    }

    function navigateTo(url, pushToHistory = true) {
      const tab = getActiveTab();
      if (!tab) return;

      if (!/^https?:\/\//i.test(url)) {
        tab.securityStatus = 'bad';
        renderPageContent();
        return;
      }

      tab.url = url;
      tab.securityStatus = 'ok';
      const statusEl = document.getElementById('status');
      const summaryEl = document.getElementById('summary');
      const frameEl = document.getElementById('pageFrame');

      statusEl.textContent = t('loading');
      statusEl.className = 'status warn';
      summaryEl.textContent = t('fetching');
      frameEl.srcdoc = '<html><body><p>' + t('loading') + '</p></body></html>';

      fetch('/fetch?url=' + encodeURIComponent(url))
        .then(res => res.json())
        .then(data => {
          if (pushToHistory) {
            tab.history = tab.history.slice(0, tab.historyIndex + 1);
            tab.history.push(url);
            tab.historyIndex = tab.history.length - 1;
          }

          tab.title = data.title || t('untitled');
          tab.summary = data.summary || t('no_content');
          tab.links = data.links || [];
          tab.html = data.html || '<html><body><p>Page content unavailable.</p></body></html>';

          if (/javascript:|data:|file:|vbscript:/i.test(data.html || '')) {
            tab.securityStatus = 'warn';
          } else {
            tab.securityStatus = 'ok';
          }

          renderPageContent();
        })
        .catch(err => {
          tab.securityStatus = 'bad';
          statusEl.textContent = t('request_failed');
          statusEl.className = 'status bad';
          summaryEl.textContent = t('no_content');
          console.error(err);
        });
    }

    function goBack() {
      const tab = getActiveTab();
      if (!tab || tab.historyIndex <= 0) return;
      tab.historyIndex -= 1;
      navigateTo(tab.history[tab.historyIndex], false);
    }

    function goForward() {
      const tab = getActiveTab();
      if (!tab || tab.historyIndex >= tab.history.length - 1) return;
      tab.historyIndex += 1;
      navigateTo(tab.history[tab.historyIndex], false);
    }

    document.getElementById('fetchButton').addEventListener('click', () => {
      const url = document.getElementById('urlInput').value.trim();
      if (!url) return;
      navigateTo(url, true);
    });

    document.getElementById('urlInput').addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        const url = document.getElementById('urlInput').value.trim();
        if (!url) return;
        navigateTo(url, true);
      }
    });

    document.getElementById('backBtn').addEventListener('click', goBack);
    document.getElementById('forwardBtn').addEventListener('click', goForward);

    renderPageContent();
    updateUI();
  </script>
</body>
</html>
)HTML";
}

void handle_client(int client_socket) {
    char buffer[4096] = {0};
    ssize_t bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_socket);
        return;
    }

    string request(buffer, bytes_read);
    string response;

    if (request.find("GET / HTTP/1.1") == 0 || request.find("GET / ") == 0) {
        response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n" + build_home_page();
    } else if (request.find("GET /fetch?") == 0) {
        size_t url_start = request.find("url=");
        if (url_start == string::npos) {
            response = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nBad request";
        } else {
            size_t url_begin = url_start + 4;
            size_t space_pos = request.find(' ', url_begin);
            string encoded_url = request.substr(url_begin, space_pos - url_begin);
            string real_url = url_decode(encoded_url);

            if (!is_valid_url(real_url)) {
                string json = "{\"title\":\"" + t("error_url_blocked") + "\",\"summary\":\"" + t("error_url_blocked") + "\",\"html\":\"\",\"links\":[ ]}";
                response = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n" + json;
            } else {
                string html = fetch_url(real_url);
                string json = build_json_response(html);
                response = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n" + json;
            }
        }
    } else {
        response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n404 Not Found";
    }

    send(client_socket, response.c_str(), response.size(), 0);
    close(client_socket);
}

int main() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        cerr << "Socket creation failed" << endl;
        return 1;
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) == -1) {
        cerr << "Bind failed" << endl;
        close(server_socket);
        return 1;
    }

    if (listen(server_socket, 10) == -1) {
        cerr << "Listen failed" << endl;
        close(server_socket);
        return 1;
    }

    cout << "========================================" << endl;
    cout << "  Simple C++ Browser v2.0" << endl;
    cout << "  Multi-language Support (EN, ES, FR)" << endl;
    cout << "========================================" << endl;
    cout << "Server running on http://localhost:8080" << endl;
    cout << "Build with: g++ -std=c++17 -pthread -lssl -lcrypto main.cpp -o browser" << endl;
    cout << "Press Ctrl+C to stop" << endl;
    cout << "========================================" << endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_socket == -1) {
            cerr << "Accept failed" << endl;
            continue;
        }

        thread handle(handle_client, client_socket);
        handle.detach();
    }

    close(server_socket);
    return 0;
}
