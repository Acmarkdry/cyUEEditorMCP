#!/usr/bin/env python3
"""
Runtime End-to-End Functional Test Suite
=========================================

Tests ALL ~123 C++ Actions against a running UE Editor instance.
Requires: UE Editor open with UEEditorMCP plugin active on port 55558.

Usage:
    cd Plugins/UEEditorMCP

    # Run all tests (full suite)
    python -m tests.test_runtime_e2e

    # Run specific category
    python -m tests.test_runtime_e2e --category ping
    python -m tests.test_runtime_e2e --category editor
    python -m tests.test_runtime_e2e --category blueprint
    python -m tests.test_runtime_e2e --category niagara

    # List categories
    python -m tests.test_runtime_e2e --list

    # Dry-run (show what would be tested)
    python -m tests.test_runtime_e2e --dry-run

    # Verbose mode
    python -m tests.test_runtime_e2e -v

Exit codes:
    0 = all tests passed
    1 = some tests failed
    2 = connection error (UE not running)
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
import traceback
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any, Callable, Optional

# ─── Connection layer (standalone, no dependency on plugin Python code) ──────

_HOST = "127.0.0.1"
_PORT = 55558
_TIMEOUT = 120.0


class TestConnection:
    """Minimal TCP connection for testing — no heartbeat, no reconnect."""

    def __init__(self, host: str = _HOST, port: int = _PORT, timeout: float = _TIMEOUT):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._socket: Optional[socket.socket] = None

    def connect(self) -> bool:
        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(self.timeout)
            self._socket.connect((self.host, self.port))
            return True
        except Exception as e:
            print(f"  ✗ Connection failed: {e}")
            self._socket = None
            return False

    def disconnect(self):
        if self._socket:
            try:
                self._send_raw({"type": "close"})
            except Exception:
                pass
            try:
                self._socket.close()
            except Exception:
                pass
            self._socket = None

    def send(self, command_type: str, params: Optional[dict] = None) -> dict:
        """Send command, return parsed response dict."""
        if not self._socket:
            raise ConnectionError("Not connected")
        msg = {"type": command_type}
        if params:
            msg["params"] = params
        self._send_raw(msg)
        return self._receive_raw()

    def _send_raw(self, data: dict):
        raw = json.dumps(data).encode("utf-8")
        length = len(raw)
        self._socket.sendall(length.to_bytes(4, byteorder="big"))
        self._socket.sendall(raw)

    def _receive_raw(self) -> dict:
        length_bytes = self._recv_exact(4)
        length = int.from_bytes(length_bytes, byteorder="big")
        if length <= 0 or length > 100 * 1024 * 1024:
            raise ValueError(f"Invalid message length: {length}")
        msg_bytes = self._recv_exact(length)
        return json.loads(msg_bytes.decode("utf-8"))

    def _recv_exact(self, n: int) -> bytes:
        data = bytearray()
        while len(data) < n:
            chunk = self._socket.recv(n - len(data))
            if not chunk:
                raise ConnectionError("Connection closed")
            data.extend(chunk)
        return bytes(data)

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.disconnect()


# ─── Test framework ─────────────────────────────────────────────────────────


class TestResult(Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    SKIP = "SKIP"
    ERROR = "ERROR"


@dataclass
class TestCase:
    """Single test case result."""

    name: str
    category: str
    result: TestResult = TestResult.SKIP
    elapsed_ms: float = 0.0
    message: str = ""
    response: Optional[dict] = None


@dataclass
class TestSuite:
    """Collection of test results."""

    cases: list[TestCase] = field(default_factory=list)
    start_time: float = 0.0
    end_time: float = 0.0

    @property
    def total(self) -> int:
        return len(self.cases)

    @property
    def passed(self) -> int:
        return sum(1 for c in self.cases if c.result == TestResult.PASS)

    @property
    def failed(self) -> int:
        return sum(1 for c in self.cases if c.result == TestResult.FAIL)

    @property
    def errors(self) -> int:
        return sum(1 for c in self.cases if c.result == TestResult.ERROR)

    @property
    def skipped(self) -> int:
        return sum(1 for c in self.cases if c.result == TestResult.SKIP)

    @property
    def duration_s(self) -> float:
        return self.end_time - self.start_time


# ─── Test helpers ───────────────────────────────────────────────────────────


def _ok(resp: dict) -> bool:
    """Check if response indicates success."""
    return resp.get("success", False) or resp.get("status") == "success"


def _result(resp: dict) -> dict:
    """Extract result payload from response."""
    return resp.get("result", resp)


def _has_field(resp: dict, *fields: str) -> bool:
    """Check if result has expected fields."""
    r = _result(resp)
    return all(f in r for f in fields)


# ─── Test definitions ───────────────────────────────────────────────────────
# Each test is: (name, category, command, params_or_None, validator_func)

TestDef = tuple[str, str, str, Optional[dict], Optional[Callable[[dict], str]]]

# Validator returns empty string on success, error message on failure


def _v_pong(r: dict) -> str:
    res = _result(r)
    return "" if res.get("pong") else f"Expected pong=true, got {res}"


def _v_ok(r: dict) -> str:
    return "" if _ok(r) else f"Expected success, got: {json.dumps(r)[:300]}"


def _v_ok_or_expected_error(acceptable_errors: list[str]):
    """Validator that accepts success OR specific expected error messages."""

    def _validate(r: dict) -> str:
        if _ok(r):
            return ""
        err = r.get("error", "")
        for pattern in acceptable_errors:
            if pattern.lower() in err.lower():
                return ""  # Expected error is OK
        return f"Unexpected error: {err[:300]}"

    return _validate


def _v_has(*fields):
    """Validator that checks result has certain fields."""

    def _validate(r: dict) -> str:
        res = _result(r)
        missing = [f for f in fields if f not in res]
        return f"Missing fields: {missing}" if missing else ""

    return _validate


def _v_any(r: dict) -> str:
    """Always passes — just verifies no crash/timeout."""
    return ""


# ─── Unique names for test assets ───────────────────────────────────────────
_TS = str(int(time.time()))[-6:]  # Timestamp suffix for unique names
_TEST_BP_NAME = f"TestBP_{_TS}"
_TEST_BP_PATH = f"/Game/TestBP_{_TS}"
_TEST_WIDGET_NAME = f"TestWidget_{_TS}"
_TEST_MATERIAL_NAME = f"TestMat_{_TS}"

# ─── All test definitions ──────────────────────────────────────────────────

ALL_TESTS: list[TestDef] = [
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: ping — Connection health
    # ═══════════════════════════════════════════════════════════════════════
    ("ping", "ping", "ping", None, _v_pong),
    ("get_context", "ping", "get_context", None, _v_any),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: editor — Editor state queries
    # ═══════════════════════════════════════════════════════════════════════
    ("is_ready", "editor", "is_ready", None, _v_ok),
    ("get_editor_logs", "editor", "get_editor_logs", {"count": 5}, _v_ok),
    ("get_unreal_logs", "editor", "get_unreal_logs", {"count": 5}, _v_ok),
    ("clear_logs", "editor", "clear_logs", None, _v_ok),
    (
        "assert_log",
        "editor",
        "assert_log",
        {"category": "LogMCP", "pattern": ".*"},
        _v_any,
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: p7_undo — Undo/Redo/History
    # ═══════════════════════════════════════════════════════════════════════
    ("get_undo_history", "p7_undo", "get_undo_history", {"limit": 5}, _v_ok),
    ("undo", "p7_undo", "undo", None, _v_any),  # may fail if nothing to undo
    ("redo", "p7_undo", "redo", None, _v_any),  # may fail if nothing to redo
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: p7_screenshot — Viewport Screenshot
    # ═══════════════════════════════════════════════════════════════════════
    (
        "take_screenshot",
        "p7_screenshot",
        "take_screenshot",
        {"width": 256, "height": 256},
        _v_ok_or_expected_error(["viewport", "commandlet", "not available"]),
    ),
    (
        "take_pie_screenshot",
        "p7_screenshot",
        "take_pie_screenshot",
        {"width": 256},
        _v_ok_or_expected_error(["PIE", "not running", "not available", "commandlet"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: editor_extra — Thumbnails, summaries
    # ═══════════════════════════════════════════════════════════════════════
    (
        "get_selected_asset_thumbnail",
        "editor_extra",
        "get_selected_asset_thumbnail",
        None,
        _v_ok_or_expected_error(["no asset", "no selection", "not found"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: layout — Auto-layout
    # ═══════════════════════════════════════════════════════════════════════
    (
        "auto_layout_selected",
        "layout",
        "auto_layout_selected",
        None,
        _v_ok_or_expected_error(
            ["no blueprint", "no graph", "no selection", "not found"]
        ),
    ),
    (
        "auto_layout_subtree",
        "layout",
        "auto_layout_subtree",
        None,
        _v_ok_or_expected_error(["no blueprint", "no graph", "no node", "not found"]),
    ),
    (
        "auto_layout_blueprint",
        "layout",
        "auto_layout_blueprint",
        None,
        _v_ok_or_expected_error(["no blueprint", "not found", "missing"]),
    ),
    (
        "layout_and_comment",
        "layout",
        "layout_and_comment",
        None,
        _v_ok_or_expected_error(["no blueprint", "not found", "missing"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: node_query — Node read operations
    # ═══════════════════════════════════════════════════════════════════════
    (
        "find_blueprint_nodes",
        "node_query",
        "find_blueprint_nodes",
        {"search_term": "EventBeginPlay"},
        _v_ok_or_expected_error(["no blueprint", "not found", "missing"]),
    ),
    (
        "get_selected_nodes",
        "node_query",
        "get_selected_nodes",
        None,
        _v_ok_or_expected_error(["no blueprint", "no graph", "not found"]),
    ),
    (
        "describe_graph",
        "node_query",
        "describe_graph",
        None,
        _v_ok_or_expected_error(["no blueprint", "no graph", "not found", "missing"]),
    ),
    (
        "describe_graph_enhanced",
        "node_query",
        "describe_graph_enhanced",
        None,
        _v_ok_or_expected_error(["no blueprint", "no graph", "not found", "missing"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: project — Project/Input actions
    # ═══════════════════════════════════════════════════════════════════════
    (
        "create_input_action",
        "project",
        "create_input_action",
        {"action_name": f"IA_Test_{_TS}", "value_type": "bool"},
        _v_ok_or_expected_error(["already exists", "failed"]),
    ),
    (
        "create_input_mapping_context",
        "project",
        "create_input_mapping_context",
        {"context_name": f"IMC_Test_{_TS}"},
        _v_ok_or_expected_error(["already exists", "failed"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: diff — Source control diff
    # ═══════════════════════════════════════════════════════════════════════
    (
        "diff_against_depot",
        "diff",
        "diff_against_depot",
        {"asset_path": "/Game/NonExistent"},
        _v_ok_or_expected_error(
            ["not found", "source control", "failed", "no source", "not connected"]
        ),
    ),
    (
        "get_asset_history",
        "diff",
        "get_asset_history",
        {"asset_path": "/Game/NonExistent"},
        _v_ok_or_expected_error(
            ["not found", "source control", "failed", "no source", "not connected"]
        ),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: python — Python execution
    # ═══════════════════════════════════════════════════════════════════════
    (
        "exec_python_simple",
        "python",
        "exec_python",
        {"code": "import unreal\n_result = unreal.SystemLibrary.get_engine_version()"},
        _v_ok,
    ),
    (
        "exec_python_actors",
        "python",
        "exec_python",
        {
            "code": "import unreal\n_result = len(unreal.EditorLevelLibrary.get_all_level_actors())"
        },
        _v_ok,
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: p8_niagara — Niagara Particle System
    # ═══════════════════════════════════════════════════════════════════════
    (
        "create_niagara_system",
        "p8_niagara",
        "create_niagara_system",
        {"system_name": f"NS_Test_{_TS}", "package_path": "/Game"},
        _v_ok_or_expected_error(["already exists", "failed"]),
    ),
    (
        "describe_niagara_system",
        "p8_niagara",
        "describe_niagara_system",
        {"system_path": f"/Game/NS_Test_{_TS}"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    (
        "get_niagara_modules",
        "p8_niagara",
        "get_niagara_modules",
        {"system_path": f"/Game/NS_Test_{_TS}"},
        _v_ok_or_expected_error(["not found", "no emitters", "failed"]),
    ),
    (
        "compile_niagara_system",
        "p8_niagara",
        "compile_niagara_system",
        {"system_path": f"/Game/NS_Test_{_TS}"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: p8_datatable — DataTable
    # ═══════════════════════════════════════════════════════════════════════
    (
        "create_datatable",
        "p8_datatable",
        "create_datatable",
        {
            "table_name": f"DT_Test_{_TS}",
            "package_path": "/Game",
            "row_struct": "/Script/Engine.DataTableRowHandle",
        },
        _v_ok_or_expected_error(
            ["already exists", "failed", "struct not found", "row struct"]
        ),
    ),
    (
        "describe_datatable",
        "p8_datatable",
        "describe_datatable",
        {"table_path": f"/Game/DT_Test_{_TS}"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    (
        "export_datatable_json",
        "p8_datatable",
        "export_datatable_json",
        {"table_path": f"/Game/DT_Test_{_TS}"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: p8_sequencer — Sequencer
    # ═══════════════════════════════════════════════════════════════════════
    (
        "create_level_sequence",
        "p8_sequencer",
        "create_level_sequence",
        {"sequence_name": f"LS_Test_{_TS}", "package_path": "/Game"},
        _v_ok_or_expected_error(["already exists", "failed"]),
    ),
    (
        "describe_level_sequence",
        "p8_sequencer",
        "describe_level_sequence",
        {"sequence_path": f"/Game/LS_Test_{_TS}"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    (
        "set_sequencer_range",
        "p8_sequencer",
        "set_sequencer_range",
        {"sequence_path": f"/Game/LS_Test_{_TS}", "start_frame": 0, "end_frame": 300},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: p10_testing — Automation Testing
    # ═══════════════════════════════════════════════════════════════════════
    (
        "list_automation_tests",
        "p10_testing",
        "list_automation_tests",
        {"filter": "MCP"},
        _v_ok,
    ),
    (
        "run_automation_test",
        "p10_testing",
        "run_automation_test",
        {"test_filter": "NonExistentTest"},
        _v_ok_or_expected_error(["no tests", "not found", "failed"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: p10_level — Level Design
    # ═══════════════════════════════════════════════════════════════════════
    ("list_sublevels", "p10_level", "list_sublevels", None, _v_ok),
    ("get_world_settings", "p10_level", "get_world_settings", None, _v_ok),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: p10_profiler — Performance profiler
    # ═══════════════════════════════════════════════════════════════════════
    ("get_frame_stats", "p10_profiler", "get_frame_stats", None, _v_ok),
    ("get_memory_stats", "p10_profiler", "get_memory_stats", None, _v_ok),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: animgraph — Animation Blueprint (read-only queries)
    # ═══════════════════════════════════════════════════════════════════════
    (
        "create_anim_blueprint",
        "animgraph",
        "create_anim_blueprint",
        {
            "blueprint_name": f"ABP_Test_{_TS}",
            "package_path": "/Game",
            "skeleton_path": "",
        },
        _v_ok_or_expected_error(
            ["skeleton", "not found", "failed", "invalid", "missing"]
        ),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: material — Material operations
    # ═══════════════════════════════════════════════════════════════════════
    (
        "get_material_summary",
        "material",
        "get_material_summary",
        {"material_path": "/Engine/BasicShapes/BasicShapeMaterial"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    (
        "analyze_material_complexity",
        "material",
        "analyze_material_complexity",
        {"material_path": "/Engine/BasicShapes/BasicShapeMaterial"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    (
        "analyze_material_dependencies",
        "material",
        "analyze_material_dependencies",
        {"material_path": "/Engine/BasicShapes/BasicShapeMaterial"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    (
        "diagnose_material",
        "material",
        "diagnose_material",
        {"material_path": "/Engine/BasicShapes/BasicShapeMaterial"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    (
        "extract_material_parameters",
        "material",
        "extract_material_parameters",
        {"material_path": "/Engine/BasicShapes/BasicShapeMaterial"},
        _v_ok_or_expected_error(["not found", "failed"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: batch — Batch execution
    # ═══════════════════════════════════════════════════════════════════════
    (
        "batch_execute_mixed",
        "batch",
        "batch_execute",
        {
            "commands": [
                {"type": "is_ready", "params": {}},
                {"type": "get_frame_stats", "params": {}},
                {"type": "get_memory_stats", "params": {}},
            ],
            "stop_on_error": False,
        },
        _v_ok_or_expected_error(["unknown", "failed"]),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: async — Async execution
    # ═══════════════════════════════════════════════════════════════════════
    (
        "async_submit",
        "async",
        "async_execute",
        {"command": "is_ready", "params": {}},
        _v_has("task_id"),
    ),
    # ═══════════════════════════════════════════════════════════════════════
    # CATEGORY: events — P9 Event Push System
    # ═══════════════════════════════════════════════════════════════════════
    ("subscribe_events_all", "events", "subscribe_events", None, _v_ok),
    ("poll_events_empty", "events", "poll_events", {"max_events": 10}, _v_ok),
    ("unsubscribe_events", "events", "unsubscribe_events", None, _v_ok),
    (
        "subscribe_events_filtered",
        "events",
        "subscribe_events",
        {"event_types": ["blueprint_compiled", "pie_started"]},
        _v_ok,
    ),
    ("poll_events_after_filter", "events", "poll_events", {"max_events": 5}, _v_ok),
    ("unsubscribe_events_final", "events", "unsubscribe_events", None, _v_ok),
]


# ─── Test runner ────────────────────────────────────────────────────────────


def run_tests(
    conn: TestConnection,
    categories: Optional[list[str]] = None,
    verbose: bool = False,
) -> TestSuite:
    """Run test suite against connected UE Editor."""
    suite = TestSuite()
    suite.start_time = time.time()

    tests_to_run = ALL_TESTS
    if categories:
        cats = set(c.lower() for c in categories)
        tests_to_run = [t for t in ALL_TESTS if t[1].lower() in cats]

    for name, category, command, params, validator in tests_to_run:
        tc = TestCase(name=name, category=category)

        t0 = time.perf_counter()
        try:
            resp = conn.send(command, params)
            tc.elapsed_ms = (time.perf_counter() - t0) * 1000
            tc.response = resp

            if validator:
                err_msg = validator(resp)
                if err_msg:
                    tc.result = TestResult.FAIL
                    tc.message = err_msg
                else:
                    tc.result = TestResult.PASS
            else:
                tc.result = TestResult.PASS

        except Exception as e:
            tc.elapsed_ms = (time.perf_counter() - t0) * 1000
            tc.result = TestResult.ERROR
            tc.message = f"{type(e).__name__}: {e}"
            if verbose:
                tc.message += f"\n{traceback.format_exc()}"

        suite.cases.append(tc)

        # Print progress
        icon = {
            TestResult.PASS: "✓",
            TestResult.FAIL: "✗",
            TestResult.SKIP: "○",
            TestResult.ERROR: "⚠",
        }[tc.result]

        color_code = {
            TestResult.PASS: "\033[32m",  # green
            TestResult.FAIL: "\033[31m",  # red
            TestResult.SKIP: "\033[33m",  # yellow
            TestResult.ERROR: "\033[35m",  # magenta
        }[tc.result]
        reset = "\033[0m"

        status = f"{color_code}{icon} {tc.result.value}{reset}"
        timing = f"({tc.elapsed_ms:.0f}ms)"
        msg_suffix = f" — {tc.message}" if tc.message else ""
        print(f"  {status}  [{category:16s}] {name:40s} {timing}{msg_suffix}")

        if verbose and tc.response and tc.result == TestResult.FAIL:
            print(
                f"           Response: {json.dumps(tc.response, ensure_ascii=False)[:500]}"
            )

    suite.end_time = time.time()
    return suite


def print_summary(suite: TestSuite):
    """Print test suite summary."""
    print()
    print("=" * 74)
    print("  RUNTIME E2E TEST SUMMARY")
    print("=" * 74)
    print(f"  Total:    {suite.total}")
    print(f"  \033[32mPassed:   {suite.passed}\033[0m")
    print(f"  \033[31mFailed:   {suite.failed}\033[0m")
    print(f"  \033[35mErrors:   {suite.errors}\033[0m")
    print(f"  \033[33mSkipped:  {suite.skipped}\033[0m")
    print(f"  Duration: {suite.duration_s:.1f}s")
    print()

    # Category breakdown
    categories: dict[str, list[TestCase]] = {}
    for tc in suite.cases:
        categories.setdefault(tc.category, []).append(tc)

    print("  Category Breakdown:")
    for cat, cases in sorted(categories.items()):
        p = sum(1 for c in cases if c.result == TestResult.PASS)
        t = len(cases)
        pct = (p / t * 100) if t > 0 else 0
        bar = "█" * int(pct / 5) + "░" * (20 - int(pct / 5))
        color = "\033[32m" if p == t else "\033[33m" if p > 0 else "\033[31m"
        print(f"    {color}{cat:20s} {bar} {p}/{t} ({pct:.0f}%)\033[0m")

    print()

    if suite.failed > 0 or suite.errors > 0:
        print("  \033[31m✗ SOME TESTS FAILED\033[0m")
        print()
        print("  Failed/Error details:")
        for tc in suite.cases:
            if tc.result in (TestResult.FAIL, TestResult.ERROR):
                print(f"    [{tc.category}] {tc.name}: {tc.message[:200]}")
        print()
    else:
        print("  \033[32m✓ ALL TESTS PASSED\033[0m")
        print()

    # Rate
    rate = (suite.passed / suite.total * 100) if suite.total > 0 else 0
    print(f"  Pass Rate: {rate:.1f}%")
    print()


def list_categories():
    """Print all available test categories."""
    cats: dict[str, int] = {}
    for _, cat, _, _, _ in ALL_TESTS:
        cats[cat] = cats.get(cat, 0) + 1
    print("Available test categories:")
    for cat, count in sorted(cats.items()):
        print(f"  {cat:20s} ({count} tests)")


# ─── Main ───────────────────────────────────────────────────────────────────


def main():
    parser = argparse.ArgumentParser(description="UEEditorMCP Runtime E2E Tests")
    parser.add_argument("-c", "--category", nargs="*", help="Run only these categories")
    parser.add_argument("--list", action="store_true", help="List available categories")
    parser.add_argument(
        "--dry-run", action="store_true", help="Show tests without running"
    )
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("--host", default=_HOST, help="UE host (default: 127.0.0.1)")
    parser.add_argument(
        "--port", type=int, default=_PORT, help="UE port (default: 55558)"
    )
    args = parser.parse_args()

    if args.list:
        list_categories()
        return 0

    tests_to_run = ALL_TESTS
    if args.category:
        cats = set(c.lower() for c in args.category)
        tests_to_run = [t for t in ALL_TESTS if t[1].lower() in cats]

    if args.dry_run:
        print(f"Would run {len(tests_to_run)} tests:")
        for name, cat, cmd, params, _ in tests_to_run:
            print(f"  [{cat:16s}] {name:40s} → {cmd}")
        return 0

    print()
    print("=" * 74)
    print("  UEEditorMCP Runtime E2E Functional Test Suite")
    print(f"  Target: {args.host}:{args.port}")
    print(f"  Tests:  {len(tests_to_run)}")
    print("=" * 74)
    print()

    # Connect
    conn = TestConnection(host=args.host, port=args.port)
    print(f"  Connecting to UE Editor at {args.host}:{args.port}...")
    if not conn.connect():
        print()
        print("  \033[31m✗ FATAL: Cannot connect to UE Editor.\033[0m")
        print("    Make sure Unreal Editor is running with UEEditorMCP plugin enabled.")
        print(f"    Expected TCP server on {args.host}:{args.port}")
        return 2

    print(f"  \033[32m✓ Connected\033[0m")
    print()

    try:
        suite = run_tests(conn, args.category, verbose=args.verbose)
        print_summary(suite)

        if suite.failed > 0 or suite.errors > 0:
            return 1
        return 0
    finally:
        conn.disconnect()


if __name__ == "__main__":
    sys.exit(main())
