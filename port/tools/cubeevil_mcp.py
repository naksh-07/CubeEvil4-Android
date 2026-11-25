#!/usr/bin/env python3
"""
CubeEvil4 Agent MCP Server
Native Model Context Protocol (MCP) server for high-velocity, deterministic testing
and telemetry of the CubeEvil4 (Resident Evil 4 GameCube Android Port) project.
"""

import sys
import os
import json
import subprocess
import re
import time
from pathlib import Path

# Windows UTF-8 stdout/stderr
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

# Resolve ADB path in priority order
SDK_ADB = Path(os.environ.get("LOCALAPPDATA", "")) / r"Android\Sdk\platform-tools\adb.exe"
WINGET_ADB = Path(os.environ.get("LOCALAPPDATA", "")) / r"Microsoft\WinGet\Packages\Google.PlatformTools_Microsoft.Winget.Source_8wekyb3d8bbwe\platform-tools\adb.exe"

if SDK_ADB.is_file():
    ADB_PATH = str(SDK_ADB)
elif WINGET_ADB.is_file():
    ADB_PATH = str(WINGET_ADB)
else:
    ADB_PATH = "adb"

# Ensure deterministic repository root
WORKSPACE_ENV = os.environ.get("CUBEEVIL_WORKSPACE", r"c:\Users\Suraj\Documents\Antigravity\CubeEvil4")
REPO_ROOT = Path(WORKSPACE_ENV) if Path(WORKSPACE_ENV).is_dir() else Path(__file__).resolve().parent.parent.parent
PACKAGE_NAME = "com.ast.cubeevil4"
MAIN_ACTIVITY = "com.ast.cubeevil4/com.ast.re4decomp.MainActivity"
GAME_STORAGE_PATH = f"/sdcard/Android/data/{PACKAGE_NAME}/files/game"

# Gamepad keycode map
KEYCODES = {
    "A": "96",           # KEYCODE_BUTTON_A
    "B": "97",           # KEYCODE_BUTTON_B
    "X": "99",           # KEYCODE_BUTTON_X
    "Y": "100",          # KEYCODE_BUTTON_Y
    "L": "104",          # KEYCODE_BUTTON_L2
    "R": "105",          # KEYCODE_BUTTON_R2 (Aim)
    "Z": "103",          # KEYCODE_BUTTON_R1
    "START": "108",      # KEYCODE_BUTTON_START
    "SELECT": "109",     # KEYCODE_BUTTON_SELECT
    "UP": "19",          # KEYCODE_DPAD_UP
    "DOWN": "20",        # KEYCODE_DPAD_DOWN
    "LEFT": "21",        # KEYCODE_DPAD_LEFT
    "RIGHT": "22",       # KEYCODE_DPAD_RIGHT
    "BACK": "4",         # KEYCODE_BACK
    "ENTER": "66",       # KEYCODE_ENTER
    "WAKE": "224",       # KEYCODE_WAKEUP
}

def log_debug(msg: str):
    sys.stderr.write(f"[CubeEvil-MCP] {msg}\n")
    sys.stderr.flush()

def run_adb(args: list[str], timeout: int = 15, serial: str = None) -> tuple[str, str, int]:
    cmd = [ADB_PATH]
    if serial:
        cmd.extend(["-s", serial])
    cmd.extend(args)
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
        return res.stdout.strip(), res.stderr.strip(), res.returncode
    except subprocess.TimeoutExpired:
        return "", "ADB command timed out", 1
    except Exception as e:
        return "", str(e), 1

def get_default_serial() -> tuple[str | None, str | None]:
    out, _, code = run_adb(["devices"])
    if code != 0:
        return None, "Failed to query ADB devices."
    lines = [line.strip() for line in out.splitlines() if line.strip() and not line.startswith("List of devices")]
    devices = [line.split()[0] for line in lines if "\tdevice" in line or " device" in line]
    if not devices:
        return None, "No active authorized Android device connected via ADB."
    return devices[0], None

# --- MCP Tool Handlers ---

def tool_device_info(serial: str = None) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err, "connected": False}

    model, _, _ = run_adb(["shell", "getprop", "ro.product.model"], serial=dev)
    manufacturer, _, _ = run_adb(["shell", "getprop", "ro.product.manufacturer"], serial=dev)
    version, _, _ = run_adb(["shell", "getprop", "ro.build.version.release"], serial=dev)
    sdk, _, _ = run_adb(["shell", "getprop", "ro.build.version.sdk"], serial=dev)
    abis, _, _ = run_adb(["shell", "getprop", "ro.product.cpu.abilist"], serial=dev)
    wm_size, _, _ = run_adb(["shell", "wm", "size"], serial=dev)
    battery_raw, _, _ = run_adb(["shell", "dumpsys", "battery"], serial=dev)

    battery_level = "Unknown"
    bat_match = re.search(r"level:\s*(\d+)", battery_raw)
    if bat_match:
        battery_level = f"{bat_match.group(1)}%"

    res_match = re.search(r"Physical size:\s*(\d+x\d+)", wm_size)
    resolution = res_match.group(1) if res_match else wm_size

    return {
        "serial": dev,
        "connected": True,
        "manufacturer": manufacturer,
        "model": model,
        "android_version": version,
        "sdk": sdk,
        "supported_abis": [a.strip() for a in abis.split(",") if a.strip()],
        "resolution": resolution,
        "battery": battery_level
    }

def tool_game_status(serial: str = None) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err, "running": False}

    # Check installed version
    pkg_dump, _, _ = run_adb(["shell", "dumpsys", "package", PACKAGE_NAME], serial=dev)
    installed = "Unable to find package" not in pkg_dump and len(pkg_dump) > 0
    version_name = "Unknown"
    abi = "Unknown"
    if installed:
        v_match = re.search(r"versionName=([^\s]+)", pkg_dump)
        if v_match:
            version_name = v_match.group(1)
        abi_match = re.search(r"primaryCpuAbi=([^\s]+)", pkg_dump)
        if abi_match:
            abi = abi_match.group(1)

    # Check running processes
    app_pid, _, _ = run_adb(["shell", "pidof", PACKAGE_NAME], serial=dev)
    core_pid, _, _ = run_adb(["shell", "pidof", f"{PACKAGE_NAME}:core"], serial=dev)

    # Memory & thread info if running
    mem_info = {}
    if app_pid:
        mem_dump, _, _ = run_adb(["shell", "dumpsys", "meminfo", PACKAGE_NAME], serial=dev)
        total_pss = re.search(r"TOTAL PSS:\s*(\d+)", mem_dump)
        if total_pss:
            mem_info["total_pss_kb"] = int(total_pss.group(1))
            mem_info["total_pss_mb"] = round(int(total_pss.group(1)) / 1024, 2)

    return {
        "installed": installed,
        "package": PACKAGE_NAME,
        "version": version_name,
        "primary_abi": abi,
        "app_running": bool(app_pid),
        "app_pid": app_pid if app_pid else None,
        "core_running": bool(core_pid),
        "core_pid": core_pid if core_pid else None,
        "memory": mem_info
    }

def tool_check_storage(serial: str = None) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err}

    check_cmd = f"cd {GAME_STORAGE_PATH} 2>/dev/null && ls -la disc1 disc2 instant memcard 2>/dev/null"
    ls_out, _, _ = run_adb(["shell", check_cmd], serial=dev)

    disc1_ready, _, _ = run_adb(["shell", f"[ -f {GAME_STORAGE_PATH}/disc1/ready.properties ] && echo yes"], serial=dev)
    disc2_ready, _, _ = run_adb(["shell", f"[ -f {GAME_STORAGE_PATH}/disc2/ready.properties ] && echo yes"], serial=dev)
    disc1_fst, _, _ = run_adb(["shell", f"[ -f {GAME_STORAGE_PATH}/disc1/sys/fst.bin ] && echo yes"], serial=dev)
    disc2_fst, _, _ = run_adb(["shell", f"[ -f {GAME_STORAGE_PATH}/disc2/sys/fst.bin ] && echo yes"], serial=dev)
    memcard_files, _, _ = run_adb(["shell", f"ls {GAME_STORAGE_PATH}/memcard 2>/dev/null"], serial=dev)

    return {
        "storage_path": GAME_STORAGE_PATH,
        "disc1_imported": disc1_ready.strip() == "yes" or disc1_fst.strip() == "yes",
        "disc2_imported": disc2_ready.strip() == "yes" or disc2_fst.strip() == "yes",
        "can_boot_game": (disc1_fst.strip() == "yes" and disc2_fst.strip() == "yes"),
        "memcard_files": [f.strip() for f in memcard_files.splitlines() if f.strip()],
        "raw_listing": ls_out[:500] if ls_out else "Storage path empty or unreadable"
    }

def tool_launch_game(serial: str = None, start_room: int = None, debug_hud: bool = False, wait_sec: int = 5) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err, "success": False}

    # Wake screen
    run_adb(["shell", "input", "keyevent", KEYCODES["WAKE"]], serial=dev)
    run_adb(["shell", "wm", "dismiss-keyguard"], serial=dev)

    # Build launch command
    am_args = ["shell", "am", "start", "-n", MAIN_ACTIVITY]
    if start_room is not None:
        am_args.extend(["-e", "start_room", str(start_room)])
    if debug_hud:
        am_args.extend(["--ez", "debug_hud", "true"])

    out, err_msg, code = run_adb(am_args, serial=dev)
    if code != 0:
        return {"success": False, "error": f"Launch failed: {err_msg} {out}"}

    time.sleep(wait_sec)

    app_pid, _, _ = run_adb(["shell", "pidof", PACKAGE_NAME], serial=dev)
    core_pid, _, _ = run_adb(["shell", "pidof", f"{PACKAGE_NAME}:core"], serial=dev)

    return {
        "success": bool(app_pid),
        "app_pid": app_pid if app_pid else None,
        "core_pid": core_pid if core_pid else None,
        "message": "Game launched successfully" if app_pid else "Process failed to stay running"
    }

def tool_stop_game(serial: str = None) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err, "stopped": False}

    run_adb(["shell", "am", "force-stop", PACKAGE_NAME], serial=dev)
    time.sleep(0.5)
    app_pid, _, _ = run_adb(["shell", "pidof", PACKAGE_NAME], serial=dev)
    return {
        "package": PACKAGE_NAME,
        "stopped": not bool(app_pid),
        "status": "terminated" if not app_pid else "process_still_present"
    }

def tool_send_pad(buttons: list[str] | str, delay_ms: int = 200, serial: str = None) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err, "success": False}

    if isinstance(buttons, str):
        buttons = [buttons]

    dispatched = []
    for btn in buttons:
        btn_upper = btn.upper().strip()
        if btn_upper.startswith("WAIT:"):
            try:
                wait_time = float(btn_upper.split(":")[1]) / 1000.0
                time.sleep(wait_time)
                dispatched.append(f"WAITED_{btn_upper}")
                continue
            except Exception:
                pass

        keycode = KEYCODES.get(btn_upper, btn_upper)
        run_adb(["shell", "input", "keyevent", keycode], serial=dev)
        dispatched.append(btn_upper)
        if delay_ms > 0:
            time.sleep(delay_ms / 1000.0)

    return {
        "success": True,
        "dispatched_buttons": dispatched,
        "count": len(dispatched)
    }

def tool_get_logs(lines: int = 100, serial: str = None, filter_tag: str = None) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err}

    logcat_out, _, _ = run_adb(["logcat", "-d", "-t", str(lines), "-v", "threadtime"], serial=dev, timeout=10)
    
    # Filter for relevant tags
    tags = filter_tag if filter_tag else r"RE4|CubeEvil|com\.ast\.cubeevil4|DEBUG|AndroidRuntime|CRASH"
    pattern = re.compile(tags, re.IGNORECASE)
    
    filtered_lines = [l for l in logcat_out.splitlines() if pattern.search(l)]
    return {
        "total_lines_inspected": len(logcat_out.splitlines()),
        "matched_count": len(filtered_lines),
        "logs": filtered_lines[-lines:] if filtered_lines else logcat_out.splitlines()[-30:]
    }

def tool_get_crash_report(serial: str = None) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err, "has_crash": False}

    crash_out, _, _ = run_adb(["logcat", "-d", "-b", "crash", "-v", "threadtime"], serial=dev, timeout=10)
    tombstones, _, _ = run_adb(["shell", "ls -t /data/tombstones 2>/dev/null | head -n 3"], serial=dev)

    has_crash = bool(crash_out.strip()) or bool(tombstones.strip())
    
    # Extract SIGSEGV / stack frames if present
    stack_trace = []
    if crash_out:
        for line in crash_out.splitlines():
            if any(k in line for k in ["FATAL", "SIGSEGV", "SIGABRT", "backtrace:", "#00", "#01", "#02"]):
                stack_trace.append(line.strip())

    return {
        "has_crash": has_crash,
        "crash_buffer_lines": crash_out.splitlines()[-50:] if crash_out else [],
        "key_stack_trace": stack_trace,
        "tombstones": tombstones.split() if tombstones else []
    }

def tool_take_screenshot(filename: str = "cubeevil-shot.png", serial: str = None) -> dict:
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err, "captured": False}

    out_dir = REPO_ROOT / "port" / "build" / "shots"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / filename

    remote_tmp = "/data/local/tmp/cubeevil-shot.png"
    run_adb(["shell", "screencap", "-p", remote_tmp], serial=dev)
    _, _, pull_code = run_adb(["pull", remote_tmp, str(out_path)], serial=dev)
    run_adb(["shell", "rm", "-f", remote_tmp], serial=dev)

    if pull_code == 0 and out_path.is_file():
        return {
            "captured": True,
            "saved_to": str(out_path),
            "size_bytes": out_path.stat().st_size
        }
    return {
        "captured": False,
        "error": "Failed to capture or pull screenshot from device"
    }

def tool_run_native_suite(serial: str = None, abi: str = "arm64-v8a", repeat: int = 1) -> dict:
    """Run headless native verification suite on device without UI rendering."""
    dev, err = (serial, None) if serial else get_default_serial()
    if err:
        return {"error": err, "success": False}

    script_path = REPO_ROOT / "port" / "scripts" / "test-device.ps1"
    if not script_path.is_file():
        return {"error": f"Script not found: {script_path}", "success": False}

    cmd = [
        "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
        "-File", str(script_path),
        "-Adb", ADB_PATH,
        "-Serial", dev,
        "-Abis", abi,
        "-Repeat", str(repeat)
    ]
    res = subprocess.run(cmd, capture_output=True, text=True, cwd=str(REPO_ROOT), timeout=120)
    output = res.stdout + "\n" + res.stderr
    
    passed = "TOTAL:" in output and "0 failures" in output
    total_match = re.search(r"TOTAL:\s*(\d+)\s*checks,\s*0 failures", output)
    checks_count = int(total_match.group(1)) if total_match else 0

    return {
        "success": passed,
        "exit_code": res.returncode,
        "checks_passed": checks_count,
        "raw_output": output[-2000:]
    }

# --- Tool Schema Definitions ---

TOOLS_DEFINITIONS = [
    {
        "name": "cubeevil_device_info",
        "description": "Inspect connected Android device specs (model, ABI support, battery, resolution, Android version).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    },
    {
        "name": "cubeevil_game_status",
        "description": "Query CubeEvil4 installation and live runtime status (app PID, game core PID, memory PSS).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    },
    {
        "name": "cubeevil_check_storage",
        "description": "Verify Disc 1, Disc 2, and Memory Card storage on Android device for CubeEvil4.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    },
    {
        "name": "cubeevil_launch_game",
        "description": "Launch CubeEvil4 on device, wake screen, verify process and core spawn.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "start_room": {"type": "integer", "description": "Optional room number to fast-boot directly into."},
                "debug_hud": {"type": "boolean", "description": "Whether to enable debug HUD overlay."},
                "wait_sec": {"type": "integer", "description": "Seconds to wait before reading process state."},
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    },
    {
        "name": "cubeevil_stop_game",
        "description": "Force-stop CubeEvil4 app and clean up core game process.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    },
    {
        "name": "cubeevil_send_pad",
        "description": "Dispatch GameCube controller button inputs (A, B, X, Y, START, L, R, Z, UP, DOWN, etc.) or sequences.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "buttons": {
                    "type": "array",
                    "items": {"type": "string"},
                    "description": "List of button names, e.g. ['START'], ['A', 'WAIT:500', 'B']."
                },
                "delay_ms": {"type": "integer", "description": "Delay in milliseconds between buttons (default 200)."},
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            },
            "required": ["buttons"]
        }
    },
    {
        "name": "cubeevil_get_logs",
        "description": "Fetch real-time filtered CubeEvil4 / RE4 game telemetry and runtime logs from logcat.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "lines": {"type": "integer", "description": "Number of log lines to retrieve (default 100)."},
                "filter_tag": {"type": "string", "description": "Regex pattern for tags to filter."},
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    },
    {
        "name": "cubeevil_get_crash_report",
        "description": "Inspect logcat crash buffer and tombstones for SIGSEGV / SIGABRT stack traces.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    },
    {
        "name": "cubeevil_take_screenshot",
        "description": "Capture a fast on-demand screenshot from the phone and save to port/build/shots/.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "filename": {"type": "string", "description": "Output PNG filename."},
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    },
    {
        "name": "cubeevil_run_native_suite",
        "description": "Run headless native verification suite on device (/data/local/tmp/re4-port-tests/re4_core_tests).",
        "inputSchema": {
            "type": "object",
            "properties": {
                "abi": {"type": "string", "description": "ABI to run ('arm64-v8a' or 'armeabi-v7a')."},
                "repeat": {"type": "integer", "description": "Repetition rounds (default 1)."},
                "serial": {"type": "string", "description": "Optional ADB device serial."}
            }
        }
    }
]

# --- MCP JSON-RPC Dispatcher Loop ---

def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception:
            continue

        req_id = req.get("id")
        method = req.get("method")
        params = req.get("params", {})

        if method == "initialize":
            res = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {"tools": {}},
                    "serverInfo": {
                        "name": "cubeevil-agent-mcp",
                        "version": "1.0.0"
                    }
                }
            }
            sys.stdout.write(json.dumps(res) + "\n")
            sys.stdout.flush()

        elif method == "notifications/initialized":
            pass

        elif method == "tools/list":
            res = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "tools": TOOLS_DEFINITIONS
                }
            }
            sys.stdout.write(json.dumps(res) + "\n")
            sys.stdout.flush()

        elif method == "tools/call":
            name = params.get("name")
            args = params.get("arguments", {})

            data = None
            if name == "cubeevil_device_info":
                data = tool_device_info(args.get("serial"))
            elif name == "cubeevil_game_status":
                data = tool_game_status(args.get("serial"))
            elif name == "cubeevil_check_storage":
                data = tool_check_storage(args.get("serial"))
            elif name == "cubeevil_launch_game":
                data = tool_launch_game(
                    serial=args.get("serial"),
                    start_room=args.get("start_room"),
                    debug_hud=args.get("debug_hud", False),
                    wait_sec=args.get("wait_sec", 5)
                )
            elif name == "cubeevil_stop_game":
                data = tool_stop_game(args.get("serial"))
            elif name == "cubeevil_send_pad":
                data = tool_send_pad(
                    buttons=args.get("buttons", []),
                    delay_ms=args.get("delay_ms", 200),
                    serial=args.get("serial")
                )
            elif name == "cubeevil_get_logs":
                data = tool_get_logs(
                    lines=args.get("lines", 100),
                    serial=args.get("serial"),
                    filter_tag=args.get("filter_tag")
                )
            elif name == "cubeevil_get_crash_report":
                data = tool_get_crash_report(args.get("serial"))
            elif name == "cubeevil_take_screenshot":
                data = tool_take_screenshot(
                    filename=args.get("filename", "cubeevil-shot.png"),
                    serial=args.get("serial")
                )
            elif name == "cubeevil_run_native_suite":
                data = tool_run_native_suite(
                    serial=args.get("serial"),
                    abi=args.get("abi", "arm64-v8a"),
                    repeat=args.get("repeat", 1)
                )
            else:
                data = {"error": f"Unknown tool: {name}"}

            res = {
                "jsonrpc": "2.0",
                "id": req_id,
                "result": {
                    "content": [
                        {
                            "type": "text",
                            "text": json.dumps(data, indent=2)
                        }
                    ]
                }
            }
            sys.stdout.write(json.dumps(res) + "\n")
            sys.stdout.flush()

if __name__ == "__main__":
    main()
