/* Talking to the other laptop.
 *
 * Both machines share one ntfy.sh topic and one passphrase. Everything sent
 * is AES-256-GCM encrypted before it leaves, so the relay only ever carries
 * ciphertext — the topic being public does not matter.
 */
#ifndef AVIARY_NET_H
#define AVIARY_NET_H

#include <stddef.h>

#define LETTER_PLAIN_MAX 4300

typedef struct {
  char          topic[160];
  unsigned char key[32];
  char          name[64];
  char          bird[24];
  char          self[20];     /* this machine, so it ignores its own letters */
  int           linked;
} AvConfig;

int  av_config_load(AvConfig *c);
int  av_config_save(const AvConfig *c);
void av_config_path(char *out, size_t n);

/* a file that sits next to the config: "mark", "since", "pid", "daemon.log" */
void av_config_file(char *out, size_t n, const char *leaf);

/* pair this machine with the other one */
int  net_link(const char *topic, const char *pass, const char *name,
              const char *bird);

/* One short code carries everything. Both the topic and the encryption key are
 * derived from it, so there is no second thing to remember and nothing to type
 * more than once. */
int  net_invite(const char *name, const char *bird);        /* mints a code */
int  net_join(const char *code, const char *name, const char *bird);

/* hand a letter to the relay for the other machine to pick up */
int  net_publish(const char *text, const char *from, const char *bird);

/* block forever, waiting for letters, flying each one on the local screen */
int  net_listen(void);

/* everything about the pairing and the relay that `aviary status` reports */
void net_status_print(void);

/* the local daemon's socket, shared by every client path */
void av_socket_path(char *out, size_t n);
int  av_daemon_is_up(void);
int  av_daemon_pid(void);
int  av_daemon_stop(void);
void av_daemon_claim(void);          /* the daemon records itself here */
void av_daemon_release(void);
int  av_ensure_daemon(void);
int  av_daemon_bounce(void);         /* stop it if it is up, then start it */
int  av_local_send(const char *text, const char *from, const char *bird,
                   int depart, double fx, double fy);

#endif
