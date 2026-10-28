#pragma once

/* CS1 mailbox (include/sbnio.h) between the Palm and NIO on USB port 0. */
void mailbox_init(void);
void mailbox_poll(void);
void mailbox_command(const char *line);
