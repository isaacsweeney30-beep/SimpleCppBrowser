const urlInput = document.getElementById('urlInput');
const pageFrame = document.getElementById('pageFrame');
const summaryEl = document.getElementById('summary');
const titleEl = document.getElementById('pageTitle');
const linksEl = document.getElementById('links');
const statusEl = document.getElementById('status');

async function loadPage(url) {
  const safeUrl = url.trim();
  if (!safeUrl) return;

  statusEl.textContent = 'Loading...';
  statusEl.className = 'status warn';
  summaryEl.textContent = 'Fetching page...';

  try {
    const response = await fetch(`http://localhost:8080/fetch?url=${encodeURIComponent(safeUrl)}`);
    const data = await response.json();

    titleEl.textContent = data.title || 'Untitled page';
    summaryEl.textContent = data.summary || 'No summary available.';

    const links = data.links || [];
    linksEl.innerHTML = '';

    if (links.length === 0) {
      linksEl.innerHTML = '<li>No links found.</li>';
    } else {
      links.forEach((link) => {
        const li = document.createElement('li');
        const a = document.createElement('a');
        a.href = link;
        a.target = '_blank';
        a.rel = 'noopener noreferrer';
        a.textContent = link;
        li.appendChild(a);
        linksEl.appendChild(li);
      });
    }

    if (data.html) {
      pageFrame.srcdoc = data.html;
    }

    statusEl.textContent = 'Ready';
    statusEl.className = 'status ok';
  } catch (error) {
    statusEl.textContent = 'Connection error';
    statusEl.className = 'status bad';
    summaryEl.textContent = 'Unable to connect to the C++ browser backend on localhost:8080.';
    console.error(error);
  }
}

document.getElementById('goBtn').addEventListener('click', () => {
  loadPage(urlInput.value);
});

document.getElementById('urlInput').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    loadPage(urlInput.value);
  }
});

document.getElementById('backBtn').addEventListener('click', () => {
  const current = pageFrame.src || urlInput.value;
  if (current && current !== 'about:blank') {
    history.back();
  }
});

document.getElementById('forwardBtn').addEventListener('click', () => {
  history.forward();
});

loadPage(urlInput.value);
