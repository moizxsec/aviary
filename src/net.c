#include "net.h"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define NTFY_HOST "https://ntfy.sh"
#define NONCE_LEN 12
#define TAG_LEN   16

/* ------------------------------------------------------------- the socket -- */

void av_socket_path(char *out, size_t n) {
  const char *rt = getenv("XDG_RUNTIME_DIR");
  if (rt && *rt) {
    int len = snprintf(out, n, "%s/aviary.sock", rt);
    if (len > 0 && (size_t)len < n &&
        (size_t)len < sizeof(((struct sockaddr_un *)0)->sun_path))
      return;
  }
  snprintf(out, n, "/tmp/aviary-%u.sock", (unsigned)getuid());
}

int av_local_send(const char *text, const char *from, const char *bird,
                  int depart, double fx, double fy) {
  char path[256];
  av_socket_path(path, sizeof(path));

  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return 1;

  struct sockaddr_un a;
  memset(&a, 0, sizeof(a));
  a.sun_family = AF_UNIX;
  size_t l = strlen(path);
  if (l >= sizeof(a.sun_path)) { close(fd); return 1; }
  memcpy(a.sun_path, path, l);

  if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
    close(fd);
    return 2;                       /* no daemon listening */
  }
  dprintf(fd, "%s\t%s\t%s\t%.5f\t%.5f\n",
          from ? from : "", bird ? bird : "", depart ? "out" : "in", fx, fy);
  size_t tl = strlen(text);
  if (write(fd, text, tl) < 0) { /* nothing useful to do */ }
  shutdown(fd, SHUT_WR);
  close(fd);
  return 0;
}

/* ------------------------------------------------------------------ config -- */

void av_config_path(char *out, size_t n) {
  const char *home = getenv("HOME");
  const char *xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) snprintf(out, n, "%s/aviary/config", xdg);
  else snprintf(out, n, "%s/.config/aviary/config", home ? home : ".");
}

static void hex_encode(const unsigned char *in, size_t n, char *out) {
  static const char *H = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[i * 2] = H[in[i] >> 4];
    out[i * 2 + 1] = H[in[i] & 15];
  }
  out[n * 2] = 0;
}

static int hex_decode(const char *in, unsigned char *out, size_t n) {
  if (strlen(in) < n * 2) return 0;
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    if (sscanf(in + i * 2, "%2x", &v) != 1) return 0;
    out[i] = (unsigned char)v;
  }
  return 1;
}

int av_config_load(AvConfig *c) {
  memset(c, 0, sizeof(*c));
  snprintf(c->bird, sizeof(c->bird), "pigeon");
  char path[512];
  av_config_path(path, sizeof(path));
  FILE *f = fopen(path, "r");
  if (!f) return 0;

  char line[512];
  int have_topic = 0, have_key = 0;
  while (fgets(line, sizeof(line), f)) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = 0;
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    const char *k = line, *v = eq + 1;
    if (!strcmp(k, "topic")) { snprintf(c->topic, sizeof(c->topic), "%s", v); have_topic = 1; }
    else if (!strcmp(k, "key")) { have_key = hex_decode(v, c->key, 32); }
    else if (!strcmp(k, "name")) snprintf(c->name, sizeof(c->name), "%s", v);
    else if (!strcmp(k, "bird")) snprintf(c->bird, sizeof(c->bird), "%s", v);
    else if (!strcmp(k, "self")) snprintf(c->self, sizeof(c->self), "%s", v);
  }
  fclose(f);
  c->linked = have_topic && have_key;

  /* Both laptops run the identical `link` command, so the machine id cannot
   * come from it — mint one here the first time and keep it. Without this a
   * machine would receive its own letters straight back off the relay. */
  if (c->linked && !c->self[0]) {
    unsigned char r[8];
    if (RAND_bytes(r, sizeof(r)) == 1) {
      hex_encode(r, sizeof(r), c->self);
      av_config_save(c);
    }
  }
  return c->linked;
}

int av_config_save(const AvConfig *c) {
  char path[512];
  av_config_path(path, sizeof(path));

  char dir[512];
  snprintf(dir, sizeof(dir), "%s", path);
  char *slash = strrchr(dir, '/');
  if (slash) {
    *slash = 0;
    char build[512];
    size_t n = 0;
    for (const char *p = dir; ; p++) {
      if (*p == '/' || *p == 0) {
        build[n] = 0;
        if (n > 0) mkdir(build, 0700);
      }
      if (*p == 0) break;
      build[n++] = *p;
      if (n >= sizeof(build) - 1) break;
    }
  }

  FILE *f = fopen(path, "w");
  if (!f) { perror("aviary: config"); return 1; }
  char hex[65];
  hex_encode(c->key, 32, hex);
  fprintf(f, "topic=%s\nkey=%s\nname=%s\nbird=%s\nself=%s\n",
          c->topic, hex, c->name, c->bird, c->self);
  fclose(f);
  chmod(path, 0600);                 /* the key lives here */
  return 0;
}

/* --------------------------------------------------------------- crypto -- */

static void derive_key(const char *pass, unsigned char out[32]) {
  /* the passphrase is typed by a human, so stretch it a little */
  unsigned char buf[32];
  SHA256((const unsigned char *)pass, strlen(pass), buf);
  for (int i = 0; i < 20000; i++) {
    unsigned char tmp[32 + 8];
    memcpy(tmp, buf, 32);
    memcpy(tmp + 32, "aviary\0\0", 8);
    SHA256(tmp, sizeof(tmp), buf);
  }
  memcpy(out, buf, 32);
}

static char *b64_encode(const unsigned char *in, int n) {
  int outlen = 4 * ((n + 2) / 3) + 1;
  char *out = malloc((size_t)outlen + 1);
  if (!out) return NULL;
  int w = EVP_EncodeBlock((unsigned char *)out, in, n);
  out[w] = 0;
  return out;
}

static unsigned char *b64_decode(const char *in, int *outn) {
  size_t l = strlen(in);
  unsigned char *out = malloc(l + 4);
  if (!out) return NULL;
  int w = EVP_DecodeBlock(out, (const unsigned char *)in, (int)l);
  if (w < 0) { free(out); return NULL; }
  while (l > 0 && in[l - 1] == '=') { w--; l--; }
  *outn = w;
  return out;
}

/* nonce || tag || ciphertext, base64'd */
static char *seal(const unsigned char key[32], const char *plain) {
  int pl = (int)strlen(plain);
  unsigned char *buf = malloc((size_t)pl + NONCE_LEN + TAG_LEN);
  if (!buf) return NULL;
  unsigned char *nonce = buf, *tag = buf + NONCE_LEN, *ct = buf + NONCE_LEN + TAG_LEN;
  if (RAND_bytes(nonce, NONCE_LEN) != 1) { free(buf); return NULL; }

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len = 0, ctlen = 0, ok = 0;
  if (ctx &&
      EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_LEN, NULL) == 1 &&
      EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
      EVP_EncryptUpdate(ctx, ct, &len, (const unsigned char *)plain, pl) == 1) {
    ctlen = len;
    if (EVP_EncryptFinal_ex(ctx, ct + ctlen, &len) == 1) {
      ctlen += len;
      ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN, tag) == 1;
    }
  }
  if (ctx) EVP_CIPHER_CTX_free(ctx);
  if (!ok) { free(buf); return NULL; }

  char *b64 = b64_encode(buf, NONCE_LEN + TAG_LEN + ctlen);
  free(buf);
  return b64;
}

static char *unseal(const unsigned char key[32], const char *b64) {
  int n = 0;
  unsigned char *buf = b64_decode(b64, &n);
  if (!buf) return NULL;
  if (n <= NONCE_LEN + TAG_LEN) { free(buf); return NULL; }

  unsigned char *nonce = buf, *tag = buf + NONCE_LEN, *ct = buf + NONCE_LEN + TAG_LEN;
  int ctlen = n - NONCE_LEN - TAG_LEN;
  char *plain = malloc((size_t)ctlen + 1);
  if (!plain) { free(buf); return NULL; }

  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len = 0, plen = 0, ok = 0;
  if (ctx &&
      EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_LEN, NULL) == 1 &&
      EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
      EVP_DecryptUpdate(ctx, (unsigned char *)plain, &len, ct, ctlen) == 1) {
    plen = len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, tag) == 1 &&
        EVP_DecryptFinal_ex(ctx, (unsigned char *)plain + plen, &len) == 1) {
      plen += len;
      ok = 1;
    }
  }
  if (ctx) EVP_CIPHER_CTX_free(ctx);
  free(buf);
  if (!ok) { free(plain); return NULL; }   /* wrong passphrase, or tampered */
  plain[plen] = 0;
  return plain;
}

/* ------------------------------------------------------------- pairing -- */
/*
 * A single secret is the whole pairing. The topic and the encryption key are
 * both derived from it, with different prefixes, so the topic can be public
 * without giving anything away and there is only ever one thing to carry
 * across to the other laptop.
 */
#define SECRET_LEN 15                    /* 120 bits, and 24 base32 characters */

/* Crockford base32: exactly 32 symbols, and it leaves out I, L, O and U so
 * there is nothing to misread when this is copied across by hand. */
static const char B32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static void secret_to_code(const unsigned char *sec, char *out) {
  unsigned long long acc = 0;
  int bits = 0, w = 0, sym = 0;
  for (int i = 0; i < SECRET_LEN; i++) {
    acc = (acc << 8) | sec[i];
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      out[w++] = B32[(acc >> bits) & 31];
      if (++sym % 4 == 0 && sym < 24) out[w++] = '-';
    }
  }
  out[w] = 0;
}

static int code_to_secret(const char *code, unsigned char *sec) {
  unsigned long long acc = 0;
  int bits = 0, n = 0, sym = 0;
  for (const char *p = code; *p; p++) {
    char c = *p;
    if (c == '-' || c == ' ' || c == '_') continue;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c == 'I' || c == 'L') c = '1';   /* the usual confusions, forgiven */
    if (c == 'O') c = '0';
    const char *at = strchr(B32, c);
    if (!at || !c) return 0;
    acc = (acc << 5) | (unsigned)(at - B32);
    bits += 5;
    sym++;
    if (bits >= 8) {
      bits -= 8;
      if (n < SECRET_LEN) sec[n++] = (unsigned char)((acc >> bits) & 0xff);
    }
  }
  return (sym == 24 && n == SECRET_LEN);
}

static void derive_from_secret(const unsigned char *sec, char *topic, size_t tn,
                               unsigned char key[32]) {
  unsigned char h[32];
  unsigned char buf[64];

  memcpy(buf, "aviary-topic-v1", 15);
  memcpy(buf + 15, sec, SECRET_LEN);
  SHA256(buf, 15 + SECRET_LEN, h);
  char hex[65];
  hex_encode(h, 12, hex);
  snprintf(topic, tn, "av-%s", hex);     /* public, and says nothing */

  memcpy(buf, "aviary-key-v1", 13);
  memcpy(buf + 13, sec, SECRET_LEN);
  SHA256(buf, 13 + SECRET_LEN, h);
  /* stretch, so a leaked topic still gives no path to the key */
  for (int i = 0; i < 20000; i++) SHA256(h, 32, h);
  memcpy(key, h, 32);
}

/* Is a daemon listening on the local socket right now? */
static int daemon_is_up(void) {
  char path[256];
  av_socket_path(path, sizeof(path));
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  struct sockaddr_un a;
  memset(&a, 0, sizeof(a));
  a.sun_family = AF_UNIX;
  size_t l = strlen(path);
  if (l >= sizeof(a.sun_path)) { close(fd); return 0; }
  memcpy(a.sun_path, path, l);
  int ok = connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0;
  close(fd);
  return ok;
}

/* Pick up the new pairing without making the human work it out. systemd is the
 * usual case but there is no user session at all under a bare root shell, so
 * fall back to telling them exactly what to run. */
static void nudge_daemon(void) {
  if (system("systemctl --user restart aviary.service >/dev/null 2>&1") == 0) {
    printf("  daemon restarted, and already watching.\n");
    return;
  }
  if (daemon_is_up())
    printf("  now restart the daemon so it picks this up:\n"
           "      pkill -x aviary && (aviary daemon &)\n");
  else
    printf("  now start the daemon:\n"
           "      aviary daemon &\n");
}

static int save_pairing(const unsigned char *sec, const char *name,
                        const char *bird) {
  AvConfig c;
  memset(&c, 0, sizeof(c));
  derive_from_secret(sec, c.topic, sizeof(c.topic), c.key);
  snprintf(c.name, sizeof(c.name), "%s",
           name && *name ? name : (getenv("USER") ? getenv("USER") : ""));
  snprintf(c.bird, sizeof(c.bird), "%s", bird && *bird ? bird : "pigeon");
  unsigned char r[8];
  if (RAND_bytes(r, sizeof(r)) == 1) hex_encode(r, sizeof(r), c.self);
  c.linked = 1;
  return av_config_save(&c);
}

int net_invite(const char *name, const char *bird) {
  unsigned char sec[SECRET_LEN];
  if (RAND_bytes(sec, SECRET_LEN) != 1) {
    fprintf(stderr, "aviary: no randomness available\n");
    return 1;
  }
  if (save_pairing(sec, name, bird) != 0) return 1;

  char code[40];
  secret_to_code(sec, code);
  printf("\n  This laptop is ready.\n\n");
  printf("      \033[1m%s\033[0m\n\n", code);
  printf("  Type that on the other laptop, once:\n\n");
  printf("      aviary join %s --name <her name>\n\n", code);
  printf("  After that neither of you ever types it again.\n");
  nudge_daemon();
  printf("\n");
  return 0;
}

int net_join(const char *code, const char *name, const char *bird) {
  unsigned char sec[SECRET_LEN];
  if (!code || !code_to_secret(code, sec)) {
    fprintf(stderr, "aviary join: that does not look like a pairing code\n");
    fprintf(stderr, "  expected 24 letters and digits, like ABCD-EFGH-JKMN-PQRS-TUVW-XYZ2\n");
    return 1;
  }
  if (save_pairing(sec, name, bird) != 0) return 1;
  printf("\n  Paired.\n");
  nudge_daemon();
  printf("\n  Nothing else to do. Letters will just arrive.\n\n");
  return 0;
}

/* ---------------------------------------------------------------- ntfy -- */

int net_link(const char *topic, const char *pass, const char *name,
             const char *bird) {
  if (!topic || !*topic || !pass || !*pass) {
    fprintf(stderr, "aviary link: need a topic and a passphrase\n");
    return 1;
  }
  if (strlen(topic) < 8)
    fprintf(stderr, "aviary: warning — short topics are guessable; use a long random one\n");

  AvConfig c;
  memset(&c, 0, sizeof(c));
  snprintf(c.topic, sizeof(c.topic), "%s", topic);
  snprintf(c.name, sizeof(c.name), "%s", name && *name ? name : (getenv("USER") ? getenv("USER") : ""));
  snprintf(c.bird, sizeof(c.bird), "%s", bird && *bird ? bird : "pigeon");
  derive_key(pass, c.key);
  c.linked = 1;
  if (av_config_save(&c) != 0) return 1;

  char path[512];
  av_config_path(path, sizeof(path));
  printf("  linked.\n");
  printf("  topic      %s\n", c.topic);
  printf("  as         %s\n", c.name[0] ? c.name : "(no name)");
  printf("  default    %s\n", c.bird);
  printf("  saved to   %s  (mode 0600)\n\n", path);
  printf("  Run the exact same `aviary link` on the other laptop.\n");
  printf("  Messages are encrypted here and decrypted there; the relay only\n");
  printf("  ever sees ciphertext.\n\n");
  return 0;
}

static size_t sink(void *p, size_t sz, size_t n, void *u) {
  (void)p; (void)u;
  return sz * n;
}

int net_publish(const char *text, const char *from, const char *bird) {
  AvConfig c;
  if (!av_config_load(&c)) {
    fprintf(stderr, "aviary: not linked yet — run `aviary link <topic> <passphrase>`\n");
    return 1;
  }

  char plain[LETTER_PLAIN_MAX];
  snprintf(plain, sizeof(plain), "%s\n%s\n%s\n%s",
           c.self,
           bird && *bird ? bird : c.bird,
           from && *from ? from : c.name,
           text ? text : "");

  char *body = seal(c.key, plain);
  if (!body) { fprintf(stderr, "aviary: could not encrypt\n"); return 1; }

  char url[256];
  snprintf(url, sizeof(url), "%s/%s", NTFY_HOST, c.topic);

  CURL *ch = curl_easy_init();
  if (!ch) { free(body); return 1; }
  struct curl_slist *hdr = NULL;
  hdr = curl_slist_append(hdr, "Title: aviary");
  hdr = curl_slist_append(hdr, "Content-Type: text/plain");

  curl_easy_setopt(ch, CURLOPT_URL, url);
  curl_easy_setopt(ch, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(ch, CURLOPT_HTTPHEADER, hdr);
  curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, sink);
  curl_easy_setopt(ch, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(ch, CURLOPT_USERAGENT, "aviary/1");

  CURLcode rc = curl_easy_perform(ch);
  long code = 0;
  curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &code);
  curl_slist_free_all(hdr);
  curl_easy_cleanup(ch);
  free(body);

  if (rc != CURLE_OK) {
    fprintf(stderr, "aviary: could not reach the relay (%s)\n", curl_easy_strerror(rc));
    return 1;
  }
  if (code < 200 || code >= 300) {
    fprintf(stderr, "aviary: relay refused the letter (HTTP %ld)\n", code);
    return 1;
  }
  return 0;
}

/* ---- the listener ------------------------------------------------------ */

/* Pull one JSON string field out of a line. No parser needed: ntfy's stream is
 * one flat object per line and we only want two of its fields. */
static int json_str(const char *line, const char *key, char *out, size_t n) {
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char *p = strstr(line, pat);
  if (!p) return 0;
  p += strlen(pat);
  size_t w = 0;
  while (*p && *p != '"' && w < n - 1) {
    if (*p == '\\' && p[1]) {
      p++;
      switch (*p) {
        case 'n': out[w++] = '\n'; break;
        case 't': out[w++] = '\t'; break;
        case 'r': break;
        case 'u': {                       /* only need the ASCII range */
          unsigned v = 0;
          if (sscanf(p + 1, "%4x", &v) == 1) { if (v < 128) out[w++] = (char)v; p += 4; }
          break;
        }
        default: out[w++] = *p; break;
      }
      p++;
    } else {
      out[w++] = *p++;
    }
  }
  out[w] = 0;
  return 1;
}

static long json_num(const char *line, const char *key) {
  char pat[64];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char *p = strstr(line, pat);
  if (!p) return 0;
  return atol(p + strlen(pat));
}

typedef struct {
  AvConfig *cfg;
  char      buf[65536];
  size_t    n;
  char      last_id[64];
  long      last_time;
} Listener;

static void since_path(char *out, size_t n) {
  char cfg[400];
  av_config_path(cfg, sizeof(cfg));
  char *slash = strrchr(cfg, '/');
  if (slash) *slash = 0;
  snprintf(out, n, "%.*s/since", (int)(n > 16 ? n - 8 : 8), cfg);
}

static long since_load(void) {
  char p[512];
  since_path(p, sizeof(p));
  FILE *f = fopen(p, "r");
  if (!f) return 0;
  long v = 0;
  if (fscanf(f, "%ld", &v) != 1) v = 0;
  fclose(f);
  return v;
}

static void since_save(long t) {
  char p[512];
  since_path(p, sizeof(p));
  FILE *f = fopen(p, "w");
  if (!f) return;
  fprintf(f, "%ld\n", t);
  fclose(f);
}

static void handle_line(Listener *L, const char *line) {
  char ev[32];
  if (!json_str(line, "event", ev, sizeof(ev))) return;
  if (strcmp(ev, "message") != 0) return;      /* open / keepalive / poll_request */

  char id[64] = {0};
  json_str(line, "id", id, sizeof(id));
  if (id[0] && !strcmp(id, L->last_id)) return;       /* already flown */

  static char msg[LETTER_PLAIN_MAX * 2];
  if (!json_str(line, "message", msg, sizeof(msg))) return;

  char *plain = unseal(L->cfg->key, msg);
  if (!plain) {
    fprintf(stderr, "[aviary] a letter arrived that this passphrase cannot open\n");
    return;
  }

  /* self \n bird \n from \n text */
  char *nl1 = strchr(plain, '\n');
  if (!nl1) { free(plain); return; }
  *nl1 = 0;
  char *nl2 = strchr(nl1 + 1, '\n');
  if (!nl2) { free(plain); return; }
  *nl2 = 0;
  char *nl3 = strchr(nl2 + 1, '\n');
  if (!nl3) { free(plain); return; }
  *nl3 = 0;

  const char *sender = plain;
  const char *bird = nl1 + 1;
  const char *from = nl2 + 1;
  const char *text = nl3 + 1;

  if (L->cfg->self[0] && !strcmp(sender, L->cfg->self)) {
    /* our own letter, come back around the loop */
    snprintf(L->last_id, sizeof(L->last_id), "%s", id);
    long t0 = json_num(line, "time");
    if (t0 > L->last_time) { L->last_time = t0; since_save(t0); }
    free(plain);
    return;
  }

  int rc = av_local_send(text, from, bird, 0, -1, -1);
  if (rc == 2)
    fprintf(stderr, "[aviary] letter arrived but no daemon is running to fly it\n");
  else
    fprintf(stderr, "[aviary] letter from %s — sending a %s\n",
            from[0] ? from : "somewhere", bird);

  snprintf(L->last_id, sizeof(L->last_id), "%s", id);
  long t = json_num(line, "time");
  if (t > L->last_time) { L->last_time = t; since_save(t); }
  free(plain);
}

static size_t on_data(void *ptr, size_t sz, size_t nm, void *user) {
  Listener *L = user;
  size_t total = sz * nm;
  const char *in = ptr;

  for (size_t i = 0; i < total; i++) {
    char ch = in[i];
    if (ch == '\n') {
      L->buf[L->n] = 0;
      if (L->n > 2) handle_line(L, L->buf);
      L->n = 0;
    } else if (L->n < sizeof(L->buf) - 1) {
      L->buf[L->n++] = ch;
    }
  }
  return total;
}

static volatile sig_atomic_t listening = 1;
static void on_stop(int s) { (void)s; listening = 0; }

int net_listen(void) {
  AvConfig cfg;
  if (!av_config_load(&cfg)) {
    fprintf(stderr, "aviary listen: not linked — run `aviary link <topic> <passphrase>`\n");
    return 1;
  }

  signal(SIGINT, on_stop);
  signal(SIGTERM, on_stop);
  signal(SIGPIPE, SIG_IGN);
  curl_global_init(CURL_GLOBAL_DEFAULT);

  Listener L;
  memset(&L, 0, sizeof(L));
  L.cfg = &cfg;
  L.last_time = since_load();

  fprintf(stderr, "[aviary] listening on %s/%s\n", NTFY_HOST, cfg.topic);

  int backoff = 1;
  while (listening) {
    /* Pick up anything sent while this machine was off, then stay connected.
     * ntfy holds recent messages, so a letter waits rather than being lost. */
    long since = L.last_time > 0 ? L.last_time + 1 : (long)time(NULL);
    char url[512];
    snprintf(url, sizeof(url), "%s/%s/json?since=%ld", NTFY_HOST, cfg.topic, since);

    CURL *ch = curl_easy_init();
    if (!ch) break;
    curl_easy_setopt(ch, CURLOPT_URL, url);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &L);
    curl_easy_setopt(ch, CURLOPT_TIMEOUT, 0L);          /* stay open */
    curl_easy_setopt(ch, CURLOPT_TCP_KEEPALIVE, 1L);
    /* ntfy sends a keepalive every ~45s; if nothing arrives for two minutes
     * the connection is dead and we reconnect */
    curl_easy_setopt(ch, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(ch, CURLOPT_LOW_SPEED_TIME, 120L);
    curl_easy_setopt(ch, CURLOPT_USERAGENT, "aviary/1");

    CURLcode rc = curl_easy_perform(ch);
    curl_easy_cleanup(ch);
    if (!listening) break;

    if (rc == CURLE_OK) backoff = 1;
    else {
      fprintf(stderr, "[aviary] relay dropped (%s), retrying in %ds\n",
              curl_easy_strerror(rc), backoff);
    }
    for (int i = 0; i < backoff && listening; i++) sleep(1);
    if (backoff < 30) backoff *= 2;
  }

  curl_global_cleanup();
  return 0;
}
