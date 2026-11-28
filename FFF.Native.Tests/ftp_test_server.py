#!/usr/bin/env python3
"""Minimal read-only FTP server for the P5 FTP smoke test.

Supports the subset FFmpeg's ftp protocol uses: USER/PASS, TYPE, SIZE, PASV,
EPSV, REST, RETR, CWD/PWD and QUIT. It serves one local file and never writes.
"""
import argparse
import os
import socket
import socketserver


class FtpHandler(socketserver.StreamRequestHandler):
    root = "."
    passive = None
    rest = 0

    def reply(self, text):
        self.wfile.write((text + "\r\n").encode("ascii"))

    def open_passive(self):
        if self.passive is not None:
            self.passive.close()
        self.passive = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.passive.bind(("127.0.0.1", 0))
        self.passive.listen(1)
        return self.passive.getsockname()[1]

    def handle(self):
        self.reply("220 mini ftp ready")
        path = self.root
        while True:
            line = self.rfile.readline()
            if not line:
                return
            command, _, argument = line.decode("utf-8", "replace").strip().partition(" ")
            command = command.upper()
            if command == "USER":
                self.reply("331 password required")
            elif command == "PASS":
                self.reply("230 logged in")
            elif command in ("TYPE", "MODE", "OPTS"):
                self.reply("200 ok")
            elif command == "FEAT":
                self.reply("211-Features")
                self.reply(" SIZE")
                self.reply(" REST STREAM")
                self.reply("211 End")
            elif command == "SYST":
                self.reply("215 UNIX Type: L8")
            elif command == "PWD":
                self.reply('257 "/"')
            elif command == "CWD":
                self.reply("250 ok")
            elif command == "SIZE":
                self.reply(f"213 {os.path.getsize(path)}")
            elif command == "REST":
                self.rest = int(argument or "0")
                self.reply("350 restarting")
            elif command == "PASV":
                port = self.open_passive()
                self.reply(f"227 Entering Passive Mode (127,0,0,1,{port // 256},{port % 256})")
            elif command == "EPSV":
                port = self.open_passive()
                self.reply(f"229 Entering Extended Passive Mode (|||{port}|)")
            elif command == "RETR":
                self.reply("150 opening data connection")
                try:
                    connection, _ = self.passive.accept()
                except OSError:
                    self.reply("425 no data connection")
                    continue
                try:
                    with open(path, "rb") as source:
                        source.seek(self.rest)
                        while True:
                            chunk = source.read(64 * 1024)
                            if not chunk:
                                break
                            connection.sendall(chunk)
                finally:
                    connection.close()
                    self.passive.close()
                    self.passive = None
                    self.rest = 0
                self.reply("226 transfer complete")
            elif command == "QUIT":
                self.reply("221 bye")
                return
            else:
                self.reply("502 not implemented")


class FtpServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--file", required=True)
    parser.add_argument("--port", type=int, default=2121)
    args = parser.parse_args()
    FtpHandler.root = os.path.abspath(args.file)
    with FtpServer(("127.0.0.1", args.port), FtpHandler) as server:
        server.serve_forever()


if __name__ == "__main__":
    main()
