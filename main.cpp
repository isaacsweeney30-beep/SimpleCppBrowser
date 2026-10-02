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

    string json = "{\n";
    json += "  \"title\": \"" + escape_json(title) + "\",\n";
    json += "  \"summary\": \"" + escape_json(text) + "\",\n";
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
    } else {
        url = "http://" + url;
        parts.protocol = "http";
        parts.port = 80;
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

    freeaddrinfo(res);

    string request =
        "GET " + parts.path + " HTTP/1.1\r\n" +
        "Host: " + parts.host + "\r\n" +
        "User-Agent: SimpleCppBrowser/1.1\r\n" +
        "Connection: close\r\n\r\n";

    send(sock, request.c_str(), request.size(), 0);

    string response;
    char buffer[4096];
    while (true) {
        ssize_t bytes = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;
        response.append(buffer, bytes);
    }

    close(sock);

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
    body {
      font-family: Arial, sans-serif;
      margin: 0;
      background: #111827;
      color: #f3f4f6;
    }
    .container {
      max-width: 1100px;
      margin: 40px auto;
      padding: 20px;
    }
    .card {
      background: #1f2937;
      border-radius: 12px;
      box-shadow: 0 10px 25px rgba(0,0,0,0.2);
      padding: 20px;
    }
    h1 {
      margin-top: 0;
      font-size: 2rem;
    }
    .toolbar {
      display: flex;
      gap: 10px;
      margin-bottom: 20px;
      flex-wrap: wrap;
      align-items: center;
    }
    .nav-buttons {
      display: flex;
      gap: 8px;
    }
    button {
      background: #3b82f6;
      color: white;
      border: none;
      border-radius: 8px;
      padding: 12px 18px;
      cursor: pointer;
      font-size: 1rem;
    }
    button:hover {
      background: #2563eb;
    }
    button:disabled {
      background: #6b7280;
      cursor: not-allowed;
      opacity: 0.6;
    }
    input {
      flex: 1;
      min-width: 220px;
      padding: 12px 14px;
      border-radius: 8px;
      border: 1px solid #374151;
      background: #0f172a;
      color: #f9fafb;
      font-size: 1rem;
    }
    .meta {
      margin-bottom: 16px;
    }
    .meta h2 {
      font-size: 1.3rem;
      margin-bottom: 8px;
    }
    .summary {
      background: rgba(255,255,255,0.03);
      border: 1px solid #374151;
      border-radius: 8px;
      padding: 16px;
      line-height: 1.6;
      white-space: pre-wrap;
      margin-bottom: 18px;
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
      color: #93c5fd;
      text-decoration: none;
    }
    .status {
      color: #a7f3d0;
      font-weight: bold;
      margin-bottom: 12px;
    }
    @media (max-width: 700px) {
      .toolbar {
        flex-direction: column;
      }
      button, input {
        width: 100%;
      }
      .nav-buttons {
        width: 100%;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <h1>Simple C++ Browser</h1>
      <div class="toolbar">
        <div class="nav-buttons">
          <button id="backBtn" onclick="goBack()">← Back</button>
          <button id="forwardBtn" onclick="goForward()">Forward →</button>
        </div>
        <input id="urlInput" type="text" value="http://example.com" placeholder="Enter an address" />
        <button id="fetchButton" onclick="fetchPage()">Browse</button>
      </div>

      <div id="status" class="status">Ready</div>
      <div class="meta">
        <h2 id="pageTitle">Page title</h2>
      </div>
      <div id="summary" class="summary">Loading page...</div>
      <h3>Links</h3>
      <ul id="links" class="links"></ul>
    </div>
  </div>

  <script>
    let canGoBack = false;
    let canGoForward = false;

    async function fetchPage() {
      const input = document.getElementById('urlInput');
      const status = document.getElementById('status');
      const title = document.getElementById('pageTitle');
      const summary = document.getElementById('summary');
      const linksList = document.getElementById('links');
      const url = input.value.trim();

      if (!url) {
        status.textContent = 'Please enter a URL';
        return;
      }

      status.textContent = 'Loading...';
      summary.textContent = 'Fetching page...';
      title.textContent = 'Page title';
      linksList.innerHTML = '';

      try {
        const res = await fetch('/fetch?url=' + encodeURIComponent(url));
        const data = await res.json();

        title.textContent = data.title || 'Untitled page';
        summary.textContent = data.summary || 'No readable content found.';

        if (Array.isArray(data.links) && data.links.length > 0) {
          data.links.forEach(link => {
            const li = document.createElement('li');
            const a = document.createElement('a');
            a.href = link;
            a.target = '_blank';
            a.rel = 'noopener noreferrer';
            a.textContent = link;
            li.appendChild(a);
            linksList.appendChild(li);
          });
        } else {
          const li = document.createElement('li');
          li.textContent = 'No links were found.';
          linksList.appendChild(li);
        }

        status.textContent = 'Page loaded successfully';
        updateNavigationButtons();
      } catch (error) {
        title.textContent = 'Error';
        summary.textContent = 'Could not load the page. Check the URL and try again.';
        status.textContent = 'Request failed';
        console.error(error);
      }
    }

    async function goBack() {
      try {
        const res = await fetch('/history/back');
        const data = await res.json();
        if (data.url) {
          document.getElementById('urlInput').value = data.url;
          await loadPageContent(data.url);
          updateNavigationButtons();
        }
      } catch (error) {
        console.error(error);
      }
    }

    async function goForward() {
      try {
        const res = await fetch('/history/forward');
        const data = await res.json();
        if (data.url) {
          document.getElementById('urlInput').value = data.url;
          await loadPageContent(data.url);
          updateNavigationButtons();
        }
      } catch (error) {
        console.error(error);
      }
    }

    async function loadPageContent(url) {
      const status = document.getElementById('status');
      const title = document.getElementById('pageTitle');
      const summary = document.getElementById('summary');
      const linksList = document.getElementById('links');

      status.textContent = 'Loading...';
      summary.textContent = 'Fetching page...';
      title.textContent = 'Page title';
      linksList.innerHTML = '';

      try {
        const res = await fetch('/fetch?url=' + encodeURIComponent(url));
        const data = await res.json();

        title.textContent = data.title || 'Untitled page';
        summary.textContent = data.summary || 'No readable content found.';

        if (Array.isArray(data.links) && data.links.length > 0) {
          data.links.forEach(link => {
            const li = document.createElement('li');
            const a = document.createElement('a');
            a.href = link;
            a.target = '_blank';
            a.rel = 'noopener noreferrer';
            a.textContent = link;
            li.appendChild(a);
            linksList.appendChild(li);
          });
        } else {
          const li = document.createElement('li');
          li.textContent = 'No links were found.';
          linksList.appendChild(li);
        }

        status.textContent = 'Page loaded successfully';
      } catch (error) {
        title.textContent = 'Error';
        summary.textContent = 'Could not load the page. Check the URL and try again.';
        status.textContent = 'Request failed';
        console.error(error);
      }
    }

    async function updateNavigationButtons() {
      try {
        const res = await fetch('/history/status');
        const data = await res.json();
        document.getElementById('backBtn').disabled = !data.canGoBack;
        document.getElementById('forwardBtn').disabled = !data.canGoForward;
      } catch (error) {
        console.error(error);
      }
    }

    document.getElementById('fetchButton').addEventListener('click', fetchPage);
    document.getElementById('urlInput').addEventListener('keydown', (event) => {
      if (event.key === 'Enter') {
        fetchPage();
      }
    });

    updateNavigationButtons();
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
        response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n\r\n" +
            build_home_page();
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

            response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json; charset=utf-8\r\n"
                "Connection: close\r\n\r\n" +
                json;
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
        response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n\r\n" +
            json;
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
