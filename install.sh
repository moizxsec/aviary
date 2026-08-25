#!/usr/bin/env sh
# Aviary installer.
#   curl -fsSL <raw-url>/install.sh | sh
# or, from a clone:  ./install.sh
set -eu

PREFIX="${PREFIX:-$HOME/.local}"
BIN="$PREFIX/bin"

say() { printf '  %s\n' "$*"; }
die() { printf 'aviary: %s\n' "$*" >&2; exit 1; }

printf '\n  aviary — tiny birds that carry letters\n\n'

# ---- dependencies --------------------------------------------------------
need_pkgs=""
command -v cc >/dev/null 2>&1 || need_pkgs="$need_pkgs build-essential"
pkg-config --exists cairo 2>/dev/null || need_pkgs="$need_pkgs libcairo2-dev"
pkg-config --exists x11   2>/dev/null || need_pkgs="$need_pkgs libx11-dev"
pkg-config --exists xext  2>/dev/null || need_pkgs="$need_pkgs libxext-dev"
pkg-config --exists libcurl 2>/dev/null || need_pkgs="$need_pkgs libcurl4-openssl-dev"
pkg-config --exists libcrypto 2>/dev/null || need_pkgs="$need_pkgs libssl-dev"

if [ -n "$need_pkgs" ]; then
  say "missing build dependencies:$need_pkgs"
  if command -v apt-get >/dev/null 2>&1; then
    say "installing them (needs sudo)..."
    sudo apt-get update -qq
    # shellcheck disable=SC2086
    sudo apt-get install -y --no-install-recommends $need_pkgs
  elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y gcc cairo-devel libX11-devel libXext-devel libcurl-devel openssl-devel
  elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed --noconfirm base-devel cairo libx11 libxext curl openssl
  else
    die "install these yourself, then re-run:$need_pkgs"
  fi
fi

# ---- build ---------------------------------------------------------------
SRC="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
say "building..."
make -C "$SRC" -s clean >/dev/null 2>&1 || true
make -C "$SRC" -s

mkdir -p "$BIN"
install -m755 "$SRC/aviary" "$BIN/aviary"
say "installed $BIN/aviary"

# ---- run it on login -----------------------------------------------------
UNIT_DIR="$HOME/.config/systemd/user"
if command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
  mkdir -p "$UNIT_DIR"
  cat > "$UNIT_DIR/aviary.service" <<UNIT
[Unit]
Description=Aviary — tiny birds that carry letters
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=$BIN/aviary daemon --pixel 2
Restart=on-failure
RestartSec=3

[Install]
WantedBy=graphical-session.target
UNIT
  systemctl --user daemon-reload 2>/dev/null || true
  systemctl --user enable --now aviary.service 2>/dev/null \
    && say "daemon enabled; it will start with your session" \
    || say "could not enable the service — start it with: aviary daemon &"
else
  say "no systemd user session here (common under a root shell)."
  say "start the daemon yourself, and add this to your shell rc:"
  say "    aviary daemon >/dev/null 2>&1 &"
fi

# ---- PATH ----------------------------------------------------------------
case ":$PATH:" in
  *":$BIN:"*) ;;
  *)
    for rc in "$HOME/.bashrc" "$HOME/.zshrc"; do
      [ -f "$rc" ] || continue
      grep -qs "$BIN" "$rc" || printf '\nexport PATH="%s:$PATH"\n' "$BIN" >> "$rc"
    done
    say "added $BIN to PATH — open a new shell, or: export PATH=\"$BIN:\$PATH\""
    ;;
esac

printf '\n  done. try:\n'
printf '    aviary send "first one"      # fly one on your own screen\n'
printf '\n  to reach the other laptop:\n'
printf '    aviary invite --name <you>   # here; prints one short code\n'
printf '    aviary join <code>           # there; once, and never again\n'
printf '\n  then just:  aviary\n\n'
