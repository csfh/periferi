#pragma once

#include <stdio.h>

// Write s as a JSON string literal, quotes included. Escapes '"', '\' and every control
// byte; valid UTF-8 is passed through; each invalid byte (including a multibyte character
// cut short by truncation) becomes U+FFFD. The output is always valid JSON.
void peri_json_string(FILE *f, const char *s);
