# SimpleCppBrowser
A lightweight C++ browser prototype with a browser-style HTML/CSS/JavaScript frontend and server-side fetching logic.

## Included languages
- C++: network request handling, browser logic, HTTP server
- HTML: browser interface shell
- CSS: layout and styling
- JavaScript: frontend interaction and navigation

## Build
```bash
cmake -S . -B build
cmake --build build
./build/browser
```

## Run
Open the browser UI in a web browser and visit:

```text
http://localhost:8080
```

The C++ app serves the browser shell and handles fetch requests to remote pages.
