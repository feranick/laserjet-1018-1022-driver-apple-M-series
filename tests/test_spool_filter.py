#!/usr/bin/env python3
"""Exercise the hpzjs buffering filter without touching a printer."""

from pathlib import Path
import os
import signal
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / "src/spooling_filter.c"
payload_size = 4 * 1024 * 1024

stub_source = r"""
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
  const char *fd_text = getenv("HPZJS_LIVE_FD");
  unsigned char buf[8192];
  if (!fd_text) return 2;
  int live = atoi(fd_text);
  if (live < 3 || write(live, "LIVE:", 5) != 5) return 3;
  for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (unsigned char)i;
  size_t left = TOTAL_SIZE;
  while (left) {
    size_t n = left < sizeof(buf) ? left : sizeof(buf);
    if (write(STDOUT_FILENO, buf, n) != (ssize_t)n) return 1;
    left -= n;
  }
  return 0;
}
""".replace("TOTAL_SIZE", str(payload_size))


def run_normal(filter_binary: Path, work: Path) -> tuple[int, bytes, bytes]:
    env = os.environ.copy()
    env["TMPDIR"] = str(work)
    result = subprocess.run(
        [str(filter_binary), "1", "tester", "test", "1", "media=A4"],
        input=b"",
        capture_output=True,
        env=env,
        check=False,
        timeout=15,
    )
    return result.returncode, result.stdout, result.stderr


def run_canceled(filter_binary: Path, work: Path) -> tuple[int, int]:
    env = os.environ.copy()
    env["TMPDIR"] = str(work)
    process = subprocess.Popen(
        [str(filter_binary), "1", "tester", "test", "1", "media=A4"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=env,
    )
    assert process.stdin is not None
    assert process.stdout is not None
    process.stdin.close()

    first = process.stdout.read(4096)
    os.kill(process.pid, signal.SIGTERM)
    output = first + process.stdout.read()
    stderr = process.stderr.read() if process.stderr is not None else b""
    status = process.wait(timeout=10)
    assert first, stderr
    return status, len(output)


with tempfile.TemporaryDirectory(prefix="hpzjs-spool-test-") as name:
    directory = Path(name)
    stub = directory / "renderer"
    stub_c = directory / "renderer.c"
    filtered = directory / "filter"
    stub_c.write_text(stub_source)
    subprocess.run(
        ["cc", "-O2", "-Wall", "-Wextra", str(stub_c), "-o", str(stub)],
        check=True,
    )
    subprocess.run(
        [
            "cc",
            "-O2",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source),
            f'-DRENDERER_PATH="{stub}"',
            "-o",
            str(filtered),
        ],
        check=True,
    )

    tmp = directory / "tmp"
    tmp.mkdir()
    status, output, stderr = run_normal(filtered, tmp)
    assert status == 0, (
        f"status {status}; stderr: {stderr.decode(errors='replace')}"
    )
    assert output[:5] == b"LIVE:"
    assert len(output) == payload_size + 5
    assert not list(tmp.iterdir())

    status, output_size = run_canceled(filtered, tmp)
    assert status == 0
    assert 0 < output_size < payload_size
    assert not list(tmp.iterdir())

    env = os.environ.copy()
    env.pop("TMPDIR", None)
    missing = subprocess.run(
        [str(filtered), "1", "tester", "test", "1", ""],
        capture_output=True,
        env=env,
        check=False,
    )
    assert missing.returncode != 0
    assert b"TMPDIR is not set" in missing.stderr

print(
    "PASS: complete spool, cancellation stops output, "
    "temporary file cleanup, missing TMPDIR"
)
