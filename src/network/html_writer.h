#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include <functional>

// Streams an HTML page to the browser in chunks through a small buffer, so a
// page never has to fit in memory as a whole.
class HtmlWriter {
 public:
  // Starts a response with the given status code.
  HtmlWriter(WebServer& server, int code = 200);
  // Sends what is left in the buffer and ends the response.
  ~HtmlWriter();

  // Writes HTML as is.
  void raw(const char* html);
  void raw(const char* html, size_t length);
  void raw(const String& html) { raw(html.c_str(), html.length()); }
  // Writes text, escaped so it shows literally (also safe inside attribute
  // values).
  void text(const char* text);
  void text(const String& text) { this->text(text.c_str()); }
  void text(int number) { raw(String(number)); }

 private:
  void flush();

  WebServer& server;
  // Shared by all writers: the web server handles one request at a time, and
  // this keeps the buffer off the stack.
  static char buffer[1024];
  size_t used = 0;
};

// Fills in one placeholder, given its name without braces (e.g. "SSID" for
// {{SSID}}). Returns false for names it doesn't know.
using TemplateFiller = std::function<bool(HtmlWriter& out, const String& name)>;

// Writes a template, replacing each {{NAME}} through fill. Unknown names are
// written as they are, so a typo shows up on the page.
void writeTemplate(HtmlWriter& out, const char* html,
                   const TemplateFiller& fill);
