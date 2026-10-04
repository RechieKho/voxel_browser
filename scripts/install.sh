#!/bin/sh
# Bootstrap `vb`, the Voxel Browser developer CLI (Linux / macOS).
#
#   curl -fsSL https://github.com/RechieKho/voxel_browser/releases/latest/download/install.sh | sh
#
# Downloads the newest vb for this machine, verifies its SHA-256 against the
# release's release.toml, puts it in <data>/bin, adds that directory to your
# PATH (user-level shell profile only -- nothing system-wide, no sudo), then
# runs `vb install` for the game itself.
#
# Environment:
#   VB_HOME=<dir>          keep everything under <dir> (portable / CI)
#   VB_REPO=owner/repo     release repository   (default RechieKho/voxel_browser)
#   VB_BASE_URL=<url>      GitHub-compatible host (default https://github.com)
#   VB_RELEASE_DIR=<dir>   take release.toml + zips from a local directory instead
#   VB_PUBLIC_KEY_B64=...  base64 Ed25519 public key (SPKI DER) the release must be signed with
#                          (default: the key built into this script, if any)
#   VB_ALLOW_UNSIGNED=1    install even though the release signature cannot be checked
#   VB_NO_MODIFY_PATH=1    do not touch any shell profile
#   VB_SKIP_INSTALL=1      only install vb; do not `vb install` the game
set -eu

# Public half of the release signing key (see scripts/gen_release_key.sh). While
# empty, signatures are not checked by this script.
DEFAULT_PUBLIC_KEY_B64=""

REPO="${VB_REPO:-RechieKho/voxel_browser}"
BASE_URL="${VB_BASE_URL:-https://github.com}"

say() { printf '%s\n' "$*"; }
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "'$1' is required but not installed"; }

# ---- platform ---------------------------------------------------------------
case "$(uname -s)" in
	Linux) os=linux ;;
	Darwin) os=macos ;;
	*) die "unsupported OS '$(uname -s)' (Windows: use install.ps1)" ;;
esac
arch="$(uname -m)"
case "$os-$arch" in
	linux-x86_64 | linux-amd64) platform=linux-x86_64 ;;
	linux-aarch64 | linux-arm64) platform=linux-arm64 ;;
	macos-*) platform=macos-universal ;;
	*) die "no published builds for $os on $arch" ;;
esac

# ---- where things go (matches vb's own paths) --------------------------------
if [ -n "${VB_HOME:-}" ]; then
	data="$VB_HOME"
elif [ "$os" = macos ]; then
	data="$HOME/Library/Application Support/voxel_browser"
else
	data="${XDG_DATA_HOME:-$HOME/.local/share}/voxel_browser"
fi
bin="$data/bin"

need unzip
if command -v sha256sum >/dev/null 2>&1; then
	sha256() { sha256sum "$1" | cut -d' ' -f1; }
elif command -v shasum >/dev/null 2>&1; then
	sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
else
	die "need sha256sum or shasum to verify the download"
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/vb-install.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT INT TERM

# try_fetch <file-name> <destination>: 0 on success, 1 if it could not be fetched
try_fetch() {
	if [ -n "${VB_RELEASE_DIR:-}" ]; then
		[ -f "$VB_RELEASE_DIR/$1" ] && cp "$VB_RELEASE_DIR/$1" "$2"
	elif command -v curl >/dev/null 2>&1; then
		curl -fsSL "$BASE_URL/$REPO/releases/latest/download/$1" -o "$2"
	elif command -v wget >/dev/null 2>&1; then
		wget -q "$BASE_URL/$REPO/releases/latest/download/$1" -O "$2"
	else
		die "need curl or wget"
	fi
}
fetch() { try_fetch "$1" "$2" || die "cannot download $1"; }

# ---- find the vb archive for this platform in release.toml -------------------
say "Looking up the latest release..."
fetch release.toml "$tmp/release.toml"

# ---- authenticate release.toml -------------------------------------------------
# The manifest lists every archive's SHA-256, so checking its signature
# authenticates the whole release. This must happen here, with the user's own
# openssl: the vb we are about to install cannot vouch for itself.
pubkey="${VB_PUBLIC_KEY_B64:-$DEFAULT_PUBLIC_KEY_B64}"
if [ -n "$pubkey" ]; then
	unsigned_ok="${VB_ALLOW_UNSIGNED:-0}"
	if [ "$unsigned_ok" = 1 ]; then
		say "WARNING: VB_ALLOW_UNSIGNED=1 -- not checking the release signature"
	else
		command -v openssl >/dev/null 2>&1 || die "openssl is needed to check the release signature (or set VB_ALLOW_UNSIGNED=1 to skip the check)"
		try_fetch release.toml.sig "$tmp/release.toml.sig" || die "this release is not signed (no release.toml.sig); refusing to install"
		printf -- '-----BEGIN PUBLIC KEY-----\n%s\n-----END PUBLIC KEY-----\n' "$pubkey" > "$tmp/pub.pem"
		openssl base64 -d -A -in "$tmp/release.toml.sig" -out "$tmp/sig.bin" 2>/dev/null || die "release.toml.sig is not valid base64"
		# macOS's stock LibreSSL cannot verify raw Ed25519: fail closed rather than skip.
		if ! openssl pkeyutl -verify -pubin -inkey "$tmp/pub.pem" -rawin -in "$tmp/release.toml" -sigfile "$tmp/sig.bin" >"$tmp/verify.out" 2>&1; then
			if grep -qi "Signature Verification Failure" "$tmp/verify.out"; then
				die "the release signature does not match; refusing to install (possible tampering)"
			fi
			die "this openssl cannot verify Ed25519 signatures (need OpenSSL 3; on macOS: brew install openssl). Set VB_ALLOW_UNSIGNED=1 to install without the check."
		fi
		say "Release signature verified."
	fi
fi

entry="$(awk -v want_platform="$platform" '
	function flush() {
		if (kind == "cli" && platform == want_platform && build == "release" && file != "") {
			print file, sha; found = 1; exit
		}
		kind = platform = build = file = sha = ""
	}
	/^\[\[artifact\]\]/ { flush(); next }
	/^[a-z_0-9]+[ \t]*=/ {
		key = $1; sub(/[ \t]*=.*/, "", key)
		val = $0; sub(/^[^=]*=[ \t]*/, "", val); gsub(/"/, "", val); sub(/[ \t\r]+$/, "", val)
		if (key == "kind") kind = val
		else if (key == "platform") platform = val
		else if (key == "build") build = val
		else if (key == "file") file = val
		else if (key == "sha256") sha = val
	}
	END { if (!found) flush() }
' "$tmp/release.toml")"
[ -n "$entry" ] || die "the latest release has no vb download for $platform"
file="${entry% *}"
want_sha="${entry#* }"
case "$file" in */* | *\\* | "") die "unexpected archive name in release.toml" ;; esac
[ -n "$want_sha" ] || die "release.toml lists no checksum for $file"

# ---- download, verify, install ----------------------------------------------
say "Downloading $file..."
fetch "$file" "$tmp/$file"
got_sha="$(sha256 "$tmp/$file")"
[ "$got_sha" = "$want_sha" ] || die "SHA-256 mismatch for $file (expected $want_sha, got $got_sha); not installing"

mkdir -p "$bin"
unzip -q -o "$tmp/$file" vb -d "$tmp/extract" || die "cannot unpack $file"
chmod 755 "$tmp/extract/vb"
"$tmp/extract/vb" --version >/dev/null 2>&1 || die "the downloaded vb does not run on this machine"
mv -f "$tmp/extract/vb" "$bin/vb.new" && mv -f "$bin/vb.new" "$bin/vb"
say "Installed vb to $bin/vb"

# ---- PATH ---------------------------------------------------------------------
on_path=0
case ":$PATH:" in *":$bin:"*) on_path=1 ;; esac
if [ "$on_path" = 0 ]; then
	if [ "${VB_NO_MODIFY_PATH:-0}" = 1 ]; then
		say "Add this to your PATH:  $bin"
	else
		profile="$HOME/.profile"
		case "${SHELL:-}" in
			*/zsh) profile="${ZDOTDIR:-$HOME}/.zprofile" ;;
			*/bash) [ -f "$HOME/.bash_profile" ] && profile="$HOME/.bash_profile" || profile="$HOME/.profile" ;;
		esac
		line="export PATH=\"$bin:\$PATH\""
		if [ -f "$profile" ] && grep -Fq "$bin" "$profile" 2>/dev/null; then
			: # already there
		else
			printf '\n# Added by the Voxel Browser installer\n%s\n' "$line" >> "$profile"
			say "Added $bin to PATH in $profile (open a new shell to pick it up)"
		fi
	fi
fi

# ---- the game itself ----------------------------------------------------------
if [ "${VB_SKIP_INSTALL:-0}" != 1 ]; then
	say "Installing the game..."
	# Install the game from the same place vb itself came from.
	src="${VB_SOURCE:-}"
	if [ -z "$src" ]; then
		if [ -n "${VB_RELEASE_DIR:-}" ]; then
			src="dir:$VB_RELEASE_DIR"
		elif [ "$BASE_URL" != "https://github.com" ]; then
			src="$REPO@$BASE_URL"
		elif [ "$REPO" != "RechieKho/voxel_browser" ]; then
			src="$REPO"
		fi
	fi
	VB_SOURCE="$src" "$bin/vb" install latest || die "vb install failed (retry with: vb install)"
	"$bin/vb" shim install >/dev/null 2>&1 || true
fi
say "Done. Try:  vb host     (run a server)    vb launch     (start the game)"
