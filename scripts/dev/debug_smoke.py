"""Exercise the embedded C++ Lua host over real loopback TCP without requiring VS Code."""

import argparse
import json
import socket
import subprocess
from pathlib import Path


def exchange(connection, command, info=None, sequence="1"):
    """Send one LuaPanda protocol command with the upstream framing delimiter."""
    data = {"cmd": command, "callbackId": sequence, "info": info or {}}
    connection.sendall(json.dumps(data).encode() + b"|*|\n")


def next_message(stream):
    """Read one bounded newline frame and reject a disconnected or malformed peer."""
    line = stream.readline(1048577)
    if not line or len(line) > 1048576 or not line.endswith(b"|*|\n"):
        raise ValueError("Invalid or disconnected LuaPanda protocol peer")
    return json.loads(line[:-4])


def until(stream, command, stack=False):
    """Ignore unrelated logging/acknowledgements while bounding unexpected protocol traffic."""
    for _ in range(100):
        message = next_message(stream)
        if message["cmd"] == command and (not stack or "stack" in message):
            return message
    raise ValueError(f"Missing LuaPanda message: {command}")


def run(host, agent, script):
    """Validate actual breakpoints, stack, locals, watches, stepping and clean disconnect."""
    source = Path(script).resolve().as_posix()
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(10)
        process = subprocess.Popen(
            [str(host), source, str(agent), str(listener.getsockname()[1])],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            with listener.accept()[0] as connection:
                connection.settimeout(10)
                with connection.makefile("rb") as stream:
                    exchange(
                        connection,
                        "initSuccess",
                        {
                            "luaFileExtension": "lua",
                            "TempFilePath": "",
                            "cwd": Path(script).resolve().parent.as_posix(),
                            "useCHook": "false",
                            "stopOnEntry": "false",
                            "autoPathMode": "false",
                            "logLevel": "2",
                            "adapterVersion": "3.3.0",
                        },
                    )
                    until(stream, "initSuccess")
                    exchange(
                        connection,
                        "setBreakPoint",
                        {"path": source, "bks": [{"line": 9, "type": 2}]},
                    )
                    stopped = until(stream, "stopOnBreakpoint", stack=True)
                    assert any(
                        frame["file"].lower() == source.lower() for frame in stopped["stack"]
                    ), stopped
                    exchange(connection, "getVariable", {"varRef": "10000", "stackId": "2"})
                    locals_message = until(stream, "getVariable")
                    assert "answer" in json.dumps(locals_message) and "41" in json.dumps(
                        locals_message
                    ), locals_message
                    exchange(
                        connection, "getWatchedVariable", {"varName": "answer", "stackId": "2"}
                    )
                    watched = until(stream, "getWatchedVariable")
                    assert "41" in json.dumps(watched), watched
                    for command in ("stopOnStepIn", "stopOnStepOut", "stopOnStep"):
                        exchange(connection, command)
                        assert until(stream, command, stack=True)["stack"]
                    exchange(connection, "continue")
                    until(stream, "continue")
            output, errors = process.communicate(timeout=10)
            if process.returncode:
                raise RuntimeError(f"Lua debug host failed: {output}\n{errors}")
            assert "Completed owner-thread tick" in output, output
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()
    print(
        "Embedded Lua 5.5.1 C++: breakpoints, locals, watch, stack, step in/out/over and disconnect passed"
    )


def main():
    """Use explicit developer-tool paths, keeping generated content outside shipped assets."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", type=Path)
    parser.add_argument("agent", type=Path)
    parser.add_argument("script", type=Path)
    arguments = parser.parse_args()
    run(arguments.host, arguments.agent, arguments.script)


if __name__ == "__main__":
    main()
