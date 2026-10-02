#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <sstream>
#include <thread>
#include <cstring>
#include <cctype>
#include <deque>
#include <mutex>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

using namespace std;

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

string extract_title(const string& html) {
    smatch match;
    regex title_pattern("<title[^>]*>(.*?)</title>", regex::icase | regex::ECMAScript);
    if (regex_search(html, match, title_pattern)) {
        string title = match[1].str();
        title = regex_replace(title, regex("<[^>]+>"), " ");
        title = regex_replace(title, regex("\\s+"), " ");
        return trim(title);
    }
    return "Untitled page";
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
        if (link.rfind("http://", 0) == 0 || link.rfind("https://", 0) == 0 || link.rfind("mailto:", 0) == 0) {
            links.push_back(link);
        } else if (link.rfind("/", 0) == 0) {
            links.push_back(link);
        } else if (link.find("javascript:") == 0) {
            continue;
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

    string url = input_url;

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

string fetch_url(const string& input_url) {
    UrlParts parts = parse_url(trim(input_url));
    if (parts.host.empty()) {
        return "<error>Missing host</error>";
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
        return "<error>Socket creation failed</error>";
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        close(sock);
        freeaddrinfo(res);
        return "<error>Connection failed</error>";
    }

    string request =
        "GET " + parts.path + " HTTP/1.1\r\n" +
        "Host: " + parts.host + "\r\n" +
        "User-Agent: SimpleCppBrowser/1.3\r\n" +
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
            return "<error>SSL connection failed</error>";
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

    size_t body_start = response.find("\r\n\r\n");
    if (body_start == string::npos) {
        return response;
    }

    return response.substr(body_start + 4);
}

class BrowserHistory {
public:
    void push(const string& url) {
        history.push_back(url);
        current_index = history.size() - 1;
    }

    bool can_go_back() const {
        return current_index > 0;
    }

    bool can_go_forward() const {
        return current_index < static_cast<int>(history.size()) - 1;
    }

    string go_back() {
        if (can_go_back()) {
            --current_index;
            return history[current_index];
        }
        return "";
    }

    string go_forward() {
        if (can_go_forward()) {
            ++current_index;
            return history[current_index];
        }
        return "";
    }

    string current() const {
        if (current_index >= 0 && current_index < static_cast<int>(history.size())) {
            return history[current_index];
        }
        return "";
    }

private:
    vector<string> history;
    int current_index = -1;
};

BrowserHistory g_history;
mutex g_history_mutex;

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
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      font-family: Arial, sans-serif;
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

    .tab-bar {
      display: flex;
      align-items: center;
      gap: 8px;
      background: var(--card-alt);
      padding: 10px 12px;
      border-bottom: 1px solid var(--border);
      flex-wrap: wrap;
    }

    .tab {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      max-width: 220px;
      background: #1e293b;
      border: 1px solid var(--border);
      border-bottom: 2px solid transparent;
      border-radius: 8px 8px 0 0;
      padding: 8px 10px;
      color: var(--muted);
      cursor: pointer;
      font-size: 0.95rem;
      user-select: none;
    }

    .tab.active {
      background: var(--card);
      border-bottom-color: var(--accent);
      color: var(--text);
    }

    .tab-close {
      border: none;
      background: transparent;
      color: var(--muted);
      cursor: pointer;
      font-size: 1rem;
      padding: 0 2px;
    }

    .new-tab-btn {
      margin-left: auto;
      background: var(--accent);
      border: none;
      color: white;
      border-radius: 8px;
      padding: 8px 14px;
      cursor: pointer;
      font-weight: bold;
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
    }

    .status {
      color: var(--success);
      font-weight: bold;
      margin-bottom: 14px;
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
      <div id="tabs" class="tab-bar"></div>
      <div class="toolbar">
        <div class="nav-buttons">
          <button id="backBtn" type="button">← Back</button>
          <button id="forwardBtn" type="button">Forward →</button>
        </div>
        <input id="urlInput" type="text" value="https://example.com" placeholder="Enter an address" />
        <button id="fetchButton" type="button">Browse</button>
        <button id="newTabButton" class="new-tab-btn" type="button">+ New Tab</button>
      </div>

      <div class="main-view">
        <div class="panel">
          <div id="status" class="status">Ready</div>
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
    let tabs = [{
      id: 1,
      title: 'Page title',
      url: 'https://example.com',
      history: ['https://example.com'],
      historyIndex: 0,
      summary: 'Loading page...',
      links: [],
      html: '<html><body><p>Loading...</p></body></html>'
    }];

    let activeTabId = 1;
    let tabCounter = 1;

    function getActiveTab() {
      return tabs.find(tab => tab.id === activeTabId) || tabs[0];
    }

    function renderTabs() {
      const tabsEl = document.getElementById('tabs');
      tabsEl.innerHTML = '';

      tabs.forEach(tab => {
        const button = document.createElement('div');
        button.className = 'tab' + (tab.id === activeTabId ? ' active' : '');
        button.dataset.id = String(tab.id);

        const label = document.createElement('span');
        label.textContent = tab.title || 'New tab';
        label.style.maxWidth = '140px';
        label.style.overflow = 'hidden';
        label.style.textOverflow = 'ellipsis';
        label.style.whiteSpace = 'nowrap';

        const closeBtn = document.createElement('button');
        closeBtn.textContent = '×';
        closeBtn.className = 'tab-close';
        closeBtn.type = 'button';
        closeBtn.title = 'Close tab';
        closeBtn.addEventListener('click', (event) => {
          event.stopPropagation();
          closeTab(tab.id);
        });

        button.appendChild(label);
        button.appendChild(closeBtn);
        button.addEventListener('click', () => {
          setActiveTab(tab.id);
        });

        tabsEl.appendChild(button);
      });
    }

    function renderPageContent() {
      const tab = getActiveTab();
      const titleEl = document.getElementById('pageTitle');
      const summaryEl = document.getElementById('summary');
      const linksEl = document.getElementById('links');
      const urlEl = document.getElementById('urlInput');
      const frameEl = document.getElementById('pageFrame');

      titleEl.textContent = tab.title || 'Page title';
      summaryEl.textContent = tab.summary || 'No readable content found.';
      urlEl.value = tab.url || '';
      frameEl.srcdoc = tab.html || '<html><body><p>Render preview unavailable.</p></body></html>';

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
        li.textContent = 'No links were found.';
        linksEl.appendChild(li);
      }

      updateNavButtons();
    }

    function renderTabDisplay() {
      renderTabs();
      renderPageContent();
    }

    function setActiveTab(id) {
      const found = tabs.find(tab => tab.id === id);
      if (!found) return;
      activeTabId = id;
      renderTabDisplay();
    }

    function createTab(url = 'https://example.com') {
      tabCounter += 1;
      const newTab = {
        id: tabCounter,
        title: 'Page title',
        url: url,
        history: [url],
        historyIndex: 0,
        summary: 'Loading page...',
        links: [],
        html: '<html><body><p>Loading...</p></body></html>'
      };
      tabs.push(newTab);
      activeTabId = newTab.id;
      renderTabDisplay();
      return newTab;
    }

    function closeTab(id) {
      if (tabs.length === 1) return;
      const index = tabs.findIndex(tab => tab.id === id);
      if (index === -1) return;
      tabs.splice(index, 1);

      if (activeTabId === id) {
        activeTabId = tabs[Math.max(0, index - 1)].id;
      }
      renderTabDisplay();
    }

    function updateNavButtons() {
      const tab = getActiveTab();
      const backBtn = document.getElementById('backBtn');
      const forwardBtn = document.getElementById('forwardBtn');

      backBtn.disabled = tab.historyIndex <= 0;
      forwardBtn.disabled = tab.historyIndex >= tab.history.length - 1;
    }

    function pushHistory(url) {
      const tab = getActiveTab();
      if (!tab) return;

      const current = tab.history[tab.historyIndex];
      if (current !== url) {
        tab.history = tab.history.slice(0, tab.historyIndex + 1);
        tab.history.push(url);
        tab.historyIndex = tab.history.length - 1;
      }
      updateNavButtons();
    }

    function navigateTo(url, { pushToHistory = true } = {}) {
      const tab = getActiveTab();
      if (!tab) return;

      tab.url = url;
      const statusEl = document.getElementById('status');
      const summaryEl = document.getElementById('summary');
      const titleEl = document.getElementById('pageTitle');
      const linksEl = document.getElementById('links');
      const frameEl = document.getElementById('pageFrame');

      statusEl.textContent = 'Loading...';
      summaryEl.textContent = 'Fetching page...';
      titleEl.textContent = 'Page title';
      linksEl.innerHTML = '';
      frameEl.srcdoc = '<html><body><p>Loading page...</p></body></html>';

      fetch('/fetch?url=' + encodeURIComponent(url))
        .then(res => res.json())
        .then(data => {
          if (pushToHistory) {
            pushHistory(url);
          }

          tab.title = data.title || 'Untitled page';
          tab.summary = data.summary || 'No readable content found.';
          tab.links = data.links || [];
          tab.html = data.html || '<html><body><p>Page content unavailable.</p></body></html>';
          renderTabDisplay();
          statusEl.textContent = 'Page loaded successfully';
        })
        .catch(err => {
          statusEl.textContent = 'Request failed';
          summaryEl.textContent = 'Could not load page. Please check the URL and try again.';
          console.error(err);
        });
    }

    function goBack() {
      const tab = getActiveTab();
      if (!tab || tab.historyIndex <= 0) return;
      tab.historyIndex -= 1;
      const prevUrl = tab.history[tab.historyIndex];
      tab.url = prevUrl;
      navigateTo(prevUrl, { pushToHistory: false });
    }

    function goForward() {
      const tab = getActiveTab();
      if (!tab || tab.historyIndex >= tab.history.length - 1) return;
      tab.historyIndex += 1;
      const nextUrl = tab.history[tab.historyIndex];
      tab.url = nextUrl;
      navigateTo(nextUrl, { pushToHistory: false });
    }

    document.getElementById('fetchButton').addEventListener('click', () => {
      const url = document.getElementById('urlInput').value.trim();
      if (!url) return;
      navigateTo(url, { pushToHistory: true });
    });

    document.getElementById('urlInput').addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        const url = document.getElementById('urlInput').value.trim();
        if (!url) return;
        navigateTo(url, { pushToHistory: true });
      }
    });

    document.getElementById('backBtn').addEventListener('click', goBack);
    document.getElementById('forwardBtn').addEventListener('click', goForward);
    document.getElementById('newTabButton').addEventListener('click', () => {
      const nextTab = createTab('https://example.com');
      navigateTo(nextTab.url, { pushToHistory: true });
    });

    renderTabDisplay();
    navigateTo('https://example.com', { pushToHistory: true });
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
            string html = fetch_url(real_url);
            string json = build_json_response(html);

            {
                lock_guard<mutex> lock(g_history_mutex);
                g_history.push(real_url);
            }

            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nConnection: close\r\n\r\n" + json;
        }
    } else if (request.find("GET /history/back") == 0) {
        lock_guard<mutex> lock(g_history_mutex);
        string url = g_history.go_back();
        if (url.empty()) {
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"url\": \"\"}";
        } else {
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"url\": \"" + escape_json(url) + "\"}";
        }
    } else if (request.find("GET /history/forward") == 0) {
        lock_guard<mutex> lock(g_history_mutex);
        string url = g_history.go_forward();
        if (url.empty()) {
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"url\": \"\"}";
        } else {
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"url\": \"" + escape_json(url) + "\"}";
        }
    } else if (request.find("GET /history/status") == 0) {
        lock_guard<mutex> lock(g_history_mutex);
        bool can_back = g_history.can_go_back();
        bool can_forward = g_history.can_go_forward();
        string json = "{\"canGoBack\": " + string(can_back ? "true" : "false") + ", \"canGoForward\": " + string(can_forward ? "true" : "false") + "}";
        response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" + json;
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

    cout << "Simple C++ Browser is running on http://localhost:8080" << endl;
    cout << "Build with: g++ -std=c++17 -pthread -lssl -lcrypto main.cpp -o browser" << endl;

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
