#include "network/html_writer.h"

char HtmlWriter::buffer[1024];

HtmlWriter::HtmlWriter(WebServer& server, int code) : server(server) {
  // The length isn't known in advance, so the page goes out with chunked
  // transfer encoding.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(code, "text/html", "");
}

HtmlWriter::~HtmlWriter() {
  flush();
  server.sendContent("");  // marks the end of a chunked response
}

void HtmlWriter::flush() {
  if (used > 0) {
    server.sendContent(buffer, used);
    used = 0;
  }
}

void HtmlWriter::raw(const char* html) { raw(html, strlen(html)); }

void HtmlWriter::raw(const char* html, size_t length) {
  while (length > 0) {
    if (used == sizeof(buffer)) {
      flush();
    }
    size_t n = min(length, sizeof(buffer) - used);
    memcpy(buffer + used, html, n);
    used += n;
    html += n;
    length -= n;
  }
}

void HtmlWriter::text(const char* text) {
  for (; *text; text++) {
    switch (*text) {
      case '&':
        raw("&amp;");
        break;
      case '<':
        raw("&lt;");
        break;
      case '>':
        raw("&gt;");
        break;
      case '"':
        raw("&quot;");
        break;
      case '\'':
        raw("&#39;");
        break;
      default:
        raw(text, 1);
    }
  }
}

void writeTemplate(HtmlWriter& out, const char* html,
                   const TemplateFiller& fill) {
  while (true) {
    const char* open = strstr(html, "{{");
    const char* close = open ? strstr(open + 2, "}}") : nullptr;
    if (close == nullptr) {
      out.raw(html);
      return;
    }
    out.raw(html, open - html);
    String name(open + 2, close - open - 2);
    if (!fill(out, name)) {
      out.raw(open, close + 2 - open);
    }
    html = close + 2;
  }
}
