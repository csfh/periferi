#pragma once

#include <stdbool.h>
#include <stddef.h>

// How many bytes perid reads of one request (the buffer is one larger, for the NUL).
// A longer line is cut, and the cut line can be a different command, so the CLI
// must not send one. Both sides use this number, and so does every handler
// that copies its arguments: a buffer smaller than PERI_REQUEST_MAX + 1 would
// cut a line the CLI was allowed to send.
#define PERI_REQUEST_MAX 255

// Whether s can be one daemon token: not empty, no space, control byte or DEL.
// For a caller that wants to tell a bad word from a line that is too long.
bool peri_request_word_ok(const char *s);

// Write "<verb> <word>...\n" into buf. One word is one daemon token: a space,
// tab or other control byte would be split or would start a second line, so
// those words are refused. nwords may be 0. False, and buf empty when it can
// be, if a word is missing, the line would not fit in size, or it would be
// longer than PERI_REQUEST_MAX even when size is larger. A word is judged by
// peri_request_word_ok.
bool peri_request_line(char *buf, size_t size, const char *verb, char *const *words, int nwords);

// The line's first word is this verb. "list" matches "list" and "list --all",
// not "listen": the character after the verb is the end, a newline, or one space.
// On a match with nothing after the verb, *rest is set to null. On "verb args",
// *rest points at the arguments (a trailing newline included). On failure *rest
// is left alone. rest may be null when the caller does not care. The protocol is
// LF-only: "list\r\n" is not "list", because the CR would stay on the last argument.
bool peri_request_verb(const char *line, const char *verb, const char **rest);
