//
// cli.h - Part of a2fomu - Copyright (c) 2020-2021 Doug Eaton
//
// This file is part of a2fomu which is released under the two clause BSD
// licence.  See file LICENSE in the project root directory or visit the
// project at https://github.com/elecbrick/a2fomu for full license details.

// API for composing and executing CLI commands

#ifndef _CLI_H_
#define _CLI_H_

#define CMD_BUFFER_LEN 80
#define CLI_PROMPT "a2>"

extern int cli_active;
extern char cli_escape;

int cli(char *in, int s);
int exec(const char *script);

#endif /* _CLI_H_ */
