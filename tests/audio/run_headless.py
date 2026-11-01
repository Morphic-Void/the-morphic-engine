# Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
# License: MIT (see LICENSE file in repository root)
#
# Run a command against a private PulseAudio server with no sinks or sources.
# This exercises real backend initialization and genuine no-device completion;
# it supplies no null device and cannot demonstrate audible playback.

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python3 tests/audio/run_headless.py COMMAND [ARGUMENTS...]")
    with tempfile.TemporaryDirectory(prefix="morphic-audio-empty-") as temporary:
        directory = Path(temporary)
        socket = directory / "native"
        environment = dict(os.environ, PULSE_RUNTIME_PATH=temporary)
        # The private server needs only its native socket. Do not compete for
        # the user's session D-Bus name (or another concurrently running test).
        server_environment = dict(environment, DBUS_SESSION_BUS_ADDRESS=f"unix:path={directory / 'no-session-bus'}")
        with (directory / "server.log").open("w", encoding="utf-8") as log:
            server = subprocess.Popen([
                "pulseaudio", "-n", "--daemonize=no", "--use-pid-file=no",
                "--exit-idle-time=-1", "--disable-shm=yes",
                f"--load=module-native-protocol-unix socket={socket} auth-anonymous=1",
            ], env=server_environment, stdout=log, stderr=subprocess.STDOUT)
            try:
                for _ in range(100):
                    if socket.exists():
                        break
                    if server.poll() is not None:
                        raise RuntimeError((directory / "server.log").read_text(encoding="utf-8"))
                    time.sleep(0.05)
                else:
                    raise RuntimeError("Private PulseAudio socket did not appear")
                environment["PULSE_SERVER"] = f"unix:{socket}"
                result = subprocess.run(sys.argv[1:], env=environment)
                if result.returncode:
                    print((directory / "server.log").read_text(encoding="utf-8"), file=sys.stderr)
            finally:
                server.terminate()
                server.wait(timeout=10)
        return result.returncode


if __name__ == "__main__":
    sys.exit(main())
