#!/usr/bin/env python3
"""One-time Spotify login for the Pocketpet keychain (PKCE, no client secret needed).

Run this on your PC. It opens Spotify's login page in your browser, catches the answer on
http://127.0.0.1:8888/callback and prints (or writes) the two lines the keychain needs:

    #define SPOTIFY_CLIENT_ID "..."
    #define SPOTIFY_REFRESH_TOKEN "..."

Setup (once):
  1. https://developer.spotify.com/dashboard  ->  Create app (any name). Web API.
  2. In the app settings add this Redirect URI exactly:  http://127.0.0.1:8888/callback
  3. Copy the Client ID from the app page.
  4. Run:  python tools/spotify_auth.py YOUR_CLIENT_ID --write
     (--write puts the two lines into src/secrets.h, which Git ignores. Never commit that file.)

The refresh token lasts about 6 months. When Spotify mode says the login expired, run this again.
Needs Python 3.8+, standard library only.
"""
import argparse
import base64
import hashlib
import http.server
import json
import os
import re
import secrets
import sys
import threading
import urllib.parse
import urllib.request
import webbrowser

AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
PORT = 8888
REDIRECT_URI = f"http://127.0.0.1:{PORT}/callback"
SCOPES = "user-read-playback-state user-read-currently-playing user-modify-playback-state"


def make_verifier() -> str:
    return secrets.token_urlsafe(64)[:96]


def make_challenge(verifier: str) -> str:
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    return base64.urlsafe_b64encode(digest).decode("ascii").rstrip("=")


def build_auth_url(client_id: str, challenge: str, state: str) -> str:
    q = {
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "code_challenge_method": "S256",
        "code_challenge": challenge,
        "state": state,
        "scope": SCOPES,
    }
    return AUTH_URL + "?" + urllib.parse.urlencode(q)


def exchange_code(client_id: str, code: str, verifier: str) -> dict:
    data = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": REDIRECT_URI,
        "client_id": client_id,
        "code_verifier": verifier,
    }).encode("ascii")
    req = urllib.request.Request(TOKEN_URL, data=data, headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    result = {}

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if url.path != "/callback":
            self.send_response(404)
            self.end_headers()
            return
        q = urllib.parse.parse_qs(url.query)
        CallbackHandler.result = {k: v[0] for k, v in q.items()}
        ok = "code" in q
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        msg = "Done. You can close this tab and go back to the terminal." if ok else "Spotify returned an error. Check the terminal."
        self.wfile.write(f"<html><body style='font-family:sans-serif'><h2>Pocketpet</h2><p>{msg}</p></body></html>".encode())

    def log_message(self, *args):
        pass


def write_secrets(path: str, client_id: str, refresh: str) -> None:
    text = ""
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    for name, value in (("SPOTIFY_CLIENT_ID", client_id), ("SPOTIFY_REFRESH_TOKEN", refresh)):
        line = f'#define {name} "{value}"'
        pat = re.compile(rf"^[ \t]*#define[ \t]+{name}\b.*$", re.M)
        if pat.search(text):
            text = pat.sub(lambda m: line, text)
        else:
            if text and not text.endswith("\n"):
                text += "\n"
            text += line + "\n"
    if "#pragma once" not in text:
        text = "#pragma once\n" + text
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main() -> int:
    ap = argparse.ArgumentParser(description="Get a Spotify refresh token for the Pocketpet keychain.")
    ap.add_argument("client_id", help="Client ID from your Spotify developer dashboard app")
    ap.add_argument("--write", action="store_true", help="write the lines into src/secrets.h (gitignored)")
    args = ap.parse_args()

    verifier = make_verifier()
    state = secrets.token_urlsafe(16)
    url = build_auth_url(args.client_id, make_challenge(verifier), state)

    server = http.server.HTTPServer(("127.0.0.1", PORT), CallbackHandler)
    t = threading.Thread(target=server.handle_request, daemon=True)
    t.start()
    print("Opening Spotify login in your browser...")
    print("If it does not open, paste this link into the browser yourself:\n" + url + "\n")
    webbrowser.open(url)
    t.join(timeout=300)
    server.server_close()

    res = CallbackHandler.result
    if not res:
        print("Timed out waiting for the login. Run the script again.")
        return 1
    if res.get("state") != state:
        print("State mismatch, aborting (the login did not come from this run).")
        return 1
    if "code" not in res:
        print("Spotify said:", res.get("error", "unknown error"))
        return 1

    try:
        tok = exchange_code(args.client_id, res["code"], verifier)
    except Exception as e:  # noqa: BLE001
        print("Token request failed:", e)
        return 1
    refresh = tok.get("refresh_token")
    if not refresh:
        print("No refresh token in the answer:", {k: v for k, v in tok.items() if k != "access_token"})
        return 1

    if args.write:
        secrets_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "secrets.h")
        write_secrets(os.path.normpath(secrets_path), args.client_id, refresh)
        print("Wrote SPOTIFY_CLIENT_ID and SPOTIFY_REFRESH_TOKEN into src/secrets.h (Git ignores that file).")
    else:
        print("Add these two lines to src/secrets.h (never commit that file):\n")
        print(f'#define SPOTIFY_CLIENT_ID "{args.client_id}"')
        print(f'#define SPOTIFY_REFRESH_TOKEN "{refresh}"')
    print("\nThen build and flash. Spotify is in the main menu.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
