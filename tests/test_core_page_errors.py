import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading

ROOT = Path(__file__).resolve().parent.parent


def run(binary, port, output, scheme="http"):
    env = dict(os.environ, YOZORA_RENDER_TIMEOUT="10")
    return subprocess.run([binary, str(ROOT / "app/fonts"), str(ROOT / "app/ca-bundle.crt"),
                           f"{scheme}://127.0.0.1:{port}/", str(output)],
                          env=env, capture_output=True, text=True, timeout=30)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="yozora-page-errors-") as directory:
        output = Path(directory) / "page.png"
        refused = socket.socket()
        refused.bind(("127.0.0.1", 0))
        try:
            result = run(binary, refused.getsockname()[1], output)
            assert result.returncode and "page error 2" in result.stderr, result.stderr
        finally:
            refused.close()

        server = socket.socket()
        server.bind(("127.0.0.1", 0))
        server.listen(1)

        def reset():
            connection, _ = server.accept()
            connection.recv(4096)
            connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            connection.close()

        worker = threading.Thread(target=reset, daemon=True)
        worker.start()
        try:
            result = run(binary, server.getsockname()[1], output)
            assert result.returncode and "page error 5" in result.stderr, result.stderr
        finally:
            server.close()
            worker.join(timeout=2)

        certificate = Path(directory) / "certificate.pem"
        key = Path(directory) / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-subj", "/CN=localhost", "-keyout", str(key), "-out", str(certificate)],
                       check=True, capture_output=True, timeout=20)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)
        server = socket.socket()
        server.bind(("127.0.0.1", 0))
        server.listen(1)

        def untrusted():
            connection, _ = server.accept()
            try:
                with context.wrap_socket(connection, server_side=True):
                    pass
            except ssl.SSLError:
                connection.close()

        worker = threading.Thread(target=untrusted, daemon=True)
        worker.start()
        try:
            result = run(binary, server.getsockname()[1], output, "https")
            assert result.returncode and "page error 4" in result.stderr, result.stderr
        finally:
            server.close()
            worker.join(timeout=2)
    print("core page errors: refused, interrupted and untrusted TLS connections are classified")


if __name__ == "__main__":
    main()
