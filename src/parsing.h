#pragma once

#include <Arduino.h>

// Parses a non-negative decimal number within [min, max]; rejects anything
// else.
inline bool parseNumber(const String& s, int min, int max, int& out) {
  if (s.isEmpty() || s.length() > 5) {
    return false;
  }
  for (char c : s) {
    if (!isDigit(c)) {
      return false;
    }
  }
  out = s.toInt();
  return out >= min && out <= max;
}

// A line split into words, like argc/argv in C. Words are separated by spaces
// outside double quotes, so a quoted name like "good morning" stays one word
// (quotes included). Repeated spaces count as one.
struct ArgList {
  static const int MAX_ARGS = 8;
  int argCount = 0;
  String arguments[MAX_ARGS];

  // Returns false if the line has more than MAX_ARGS words.
  bool parse(const String& line) {
    argCount = 0;
    String word;
    bool quoted = false;
    for (char c : line) {
      if (c == ' ' && !quoted) {
        if (!add(word)) {
          return false;
        }
        continue;
      }
      if (c == '"') {
        quoted = !quoted;
      }
      word += c;
    }
    return add(word);
  }

 private:
  bool add(String& word) {
    if (word.isEmpty()) {
      return true;
    }
    if (argCount == MAX_ARGS) {
      return false;
    }
    arguments[argCount++] = word;
    word = "";
    return true;
  }
};
