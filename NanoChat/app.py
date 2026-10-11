"""NanoChat app: chat with the Nano in a browser window, no Terminal needed.

Double-click NanoChat.app (or run `python3 app.py`). It finds the Nano, gives
it internet access the same way bridge.py does, and opens a chat page at
http://127.0.0.1:8777, and to phones on the same Wi-Fi (scan the QR code on the
page). Unplugging and replugging the Nano is fine; it reconnects.
Standard library only.
"""
import glob
import json
import os
import queue
import socket
import sys
import threading
import time
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import bridge  # noqa: E402  (internet lookups + serial setup)

PORTS = range(8777, 8787)  # first free one wins


class Nano:
    """Keeps a connection to the Nano and fans its messages out to open pages."""

    def __init__(self):
        self.fd = None
        self.status = "searching"
        self.network = ""  # last "@N" report, e.g. "1111", for pages that open later
        self.pages = []  # one event queue per open chat page
        threading.Thread(target=self.run, daemon=True).start()

    def emit(self, kind, text=""):
        for q in list(self.pages):
            q.put({"kind": kind, "text": text})

    def set_status(self, status):
        self.status = status
        self.emit("status", status)

    def send(self, text):
        if self.fd is None or self.status != "online":
            return False
        os.write(self.fd, (text + "\n").encode())
        return True

    def run(self):
        while True:
            ports = sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.usbmodem*") +  # Mac
                   glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))  # Linux / Raspberry Pi
            if ports:
                try:
                    self.fd = bridge.open_port(ports[0])
                    self.set_status("connecting")
                    time.sleep(2.5)  # opening the port resets the Nano
                    os.write(self.fd, b"@HELLO\n")
                    self.read_loop(ports[0])
                except OSError:
                    pass
                finally:
                    if self.fd is not None:
                        try:
                            os.close(self.fd)
                        except OSError:
                            pass
                    self.fd = None
            self.set_status("searching")
            time.sleep(1)

    def read_loop(self, path):
        buf = b""
        ready = False  # skip startup chatter until the Nano confirms the bridge
        while True:
            data = os.read(self.fd, 256)
            if not data:
                if not os.path.exists(path):
                    raise OSError("unplugged")
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("ascii", "replace").strip()
                while text.startswith(">"):  # the Nano's "> " prompt
                    text = text[1:].lstrip()
                if text.startswith("@NET "):
                    self.emit("thinking")
                    threading.Thread(target=self.net_reply, args=(text[5:],), daemon=True).start()
                elif text.startswith("@L") and "|" in text:  # NanoChat learned a correction
                    with open(os.path.join(HERE, "train", "learned.txt"), "a") as f:
                        f.write(text[2:] + "\n")  # "intent|message", used by train.py next time
                    self.emit("learned", text[2:])
                elif text.startswith("@N") and text[2:].isdigit():
                    self.network = text[2:]
                    self.emit("network", self.network)  # which helpers are plugged in, e.g. "1011"
                elif text.startswith("@H") and text[2:].isdigit():
                    self.emit("helper", text[2:])  # a helper answered the next reply
                elif text.startswith("@F") and text[2:].isdigit():
                    self.emit("face", text[2:])  # mood of the reply just sent
                elif "@OK" in text:
                    ready = True
                    self.set_status("online")
                elif ready and text:
                    self.emit("nano", text)

    def net_reply(self, req):
        kind, _, msg = req.partition("|")
        try:
            ans = bridge.HANDLERS.get(kind, lambda m: "I don't know how to do that.")(msg)
        except Exception as e:
            ans = f"Internet error ({type(e).__name__})."
        if self.fd is not None:
            os.write(self.fd, ("@R " + bridge.ascii_only(ans)[:300] + "\n").encode())


nano = None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, code, body=b"", ctype="text/plain"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/":
            with open(os.path.join(HERE, "app.html"), "rb") as f:
                self.reply(200, f.read(), "text/html; charset=utf-8")
        elif self.path == "/phone":  # the address a phone on the same Wi-Fi can open
            self.reply(200, lan_url().encode())
        elif self.path == "/nanochat":  # lets a second launch find this one
            self.reply(200, b"nanochat")
        elif self.path == "/events":
            self.stream_events()
        else:
            self.reply(404)

    def do_POST(self):
        if self.path == "/send":
            n = int(self.headers.get("Content-Length", 0))
            text = self.rfile.read(n).decode("utf-8", "replace")
            text = bridge.ascii_only(text).replace("\n", " ")[:80]  # the Nano reads 80 chars max
            self.reply(200 if text and nano.send(text) else 503)
        elif self.path == "/quit":
            if self.client_address[0] != "127.0.0.1":  # only the Mac itself can stop the app
                return self.reply(403)
            self.reply(200)
            threading.Timer(0.3, lambda: os._exit(0)).start()
        else:
            self.reply(404)

    def stream_events(self):
        """Server-sent events: the page listens here for Nano messages."""
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        q = queue.Queue()
        q.put({"kind": "status", "text": nano.status})
        if nano.network:
            q.put({"kind": "network", "text": nano.network})
        nano.pages.append(q)
        try:
            while True:
                try:
                    event = q.get(timeout=15)
                    self.wfile.write(f"data: {json.dumps(event)}\n\n".encode())
                except queue.Empty:
                    self.wfile.write(b": keep-alive\n\n")
                self.wfile.flush()
        except OSError:
            pass  # page closed
        finally:
            nano.pages.remove(q)


def lan_url():
    """This Mac's address on the local network, e.g. http://192.168.1.20:8777/ ("" if offline)."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("192.168.0.1", 9))  # picks the Wi-Fi interface; nothing is sent
            return f"http://{s.getsockname()[0]}:{SERVER_PORT}/"
    except OSError:
        return ""


SERVER_PORT = 0


def already_running(port):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/nanochat", timeout=1) as r:
            return r.read() == b"nanochat"
    except OSError:
        return False


def main():
    global nano
    for port in PORTS:
        url = f"http://127.0.0.1:{port}/"
        try:
            server = ThreadingHTTPServer(("0.0.0.0", port), Handler)  # Mac + phones on the same Wi-Fi
            break
        except OSError:
            if already_running(port):
                webbrowser.open(url)  # just show the window again
                return
    else:
        sys.exit("No free port for NanoChat.")
    server.daemon_threads = True
    global SERVER_PORT
    SERVER_PORT = server.server_address[1]
    nano = Nano()
    if not os.environ.get("NANOCHAT_NO_BROWSER"):
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    server.serve_forever()


if __name__ == "__main__":
    main()
