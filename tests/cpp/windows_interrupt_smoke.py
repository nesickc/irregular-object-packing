"""Exercise cooperative Windows console interruption through the real CLI process."""

from __future__ import annotations

import argparse
import json
import os
import queue
import signal
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Optional


READY_MARKER = "packing scale step"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--irop", required=True, type=Path)
    parser.add_argument("--object", required=True, type=Path)
    parser.add_argument("--container", required=True, type=Path)
    parser.add_argument("--work-dir", required=True, type=Path)
    return parser.parse_args()


def fail(message: str, output: list[str]) -> int:
    print(message, file=sys.stderr)
    if output:
        print("--- irop output ---", file=sys.stderr)
        print("".join(output), file=sys.stderr, end="")
    return 1


def main() -> int:
    arguments = parse_arguments()
    if os.name != "nt":
        return fail("the Windows interrupt smoke test requires Windows", [])

    for path, description in (
        (arguments.irop, "irop executable"),
        (arguments.object, "object fixture"),
        (arguments.container, "container fixture"),
    ):
        if not path.is_file():
            return fail(f"{description} does not exist: {path}", [])

    arguments.work_dir.mkdir(parents=True, exist_ok=True)
    output_lines: list[str] = []
    process: Optional[subprocess.Popen[str]] = None

    with tempfile.TemporaryDirectory(
        prefix="interrupt-", dir=arguments.work_dir
    ) as temporary:
        output_directory = Path(temporary) / "cancelled"
        command = [
            str(arguments.irop.resolve()),
            "pack",
            "--object",
            str(arguments.object.resolve()),
            "--container",
            str(arguments.container.resolve()),
            "--count",
            "1",
            "--initial-volume-scale",
            "0.1",
            "--final-volume-scale",
            "1",
            "--scale-steps",
            "500",
            "--maximum-rotation-delta-radians",
            "0",
            "--no-adaptive-sampling",
            "--output-dir",
            str(output_directory),
        ]

        try:
            process = subprocess.Popen(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
                creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
            )
            assert process.stdout is not None

            observed_lines: queue.Queue[Optional[str]] = queue.Queue()

            def collect_output() -> None:
                assert process is not None
                assert process.stdout is not None
                for line in process.stdout:
                    output_lines.append(line)
                    observed_lines.put(line)
                observed_lines.put(None)

            reader = threading.Thread(target=collect_output, daemon=True)
            reader.start()

            readiness_deadline = time.monotonic() + 30.0
            ready = False
            while time.monotonic() < readiness_deadline:
                remaining = max(0.01, readiness_deadline - time.monotonic())
                try:
                    line = observed_lines.get(timeout=min(0.25, remaining))
                except queue.Empty:
                    if process.poll() is not None:
                        break
                    continue
                if line is None:
                    break
                if READY_MARKER in line:
                    ready = True
                    break

            if not ready:
                if process.poll() is None:
                    process.kill()
                process.wait(timeout=10.0)
                reader.join(timeout=10.0)
                return fail(
                    "irop did not reach an interruptible packing scale step",
                    output_lines,
                )

            # CREATE_NEW_PROCESS_GROUP makes CTRL_BREAK_EVENT safely targetable to
            # the child group. Windows disables targeted CTRL_C_EVENT for such a
            # group, so Ctrl+Break is the deterministic automation equivalent;
            # both SIGBREAK and SIGINT must route to the same CLI cancellation flag.
            process.send_signal(signal.CTRL_BREAK_EVENT)
            try:
                return_code = process.wait(timeout=30.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10.0)
                reader.join(timeout=10.0)
                return fail("irop did not stop after CTRL_BREAK_EVENT", output_lines)
            reader.join(timeout=10.0)

            if return_code != 130:
                return fail(
                    f"expected cancellation exit 130, got {return_code}", output_lines
                )

            summary_path = output_directory / "run-summary.json"
            if not summary_path.is_file():
                return fail(
                    "cancelled packing did not publish run-summary.json", output_lines
                )
            try:
                summary = json.loads(summary_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as error:
                return fail(
                    f"cancelled run summary is unreadable: {error}", output_lines
                )

            category = summary.get("outcome", {}).get("category")
            if category != "cancelled":
                return fail(
                    f"expected cancelled summary category, got {category!r}",
                    output_lines,
                )

            published_names = {path.name for path in output_directory.iterdir()}
            if published_names != {"run-summary.json"}:
                return fail(
                    f"cancelled run published unexpected artifacts: {sorted(published_names)}",
                    output_lines,
                )
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=10.0)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
