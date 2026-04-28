# coding: utf-8
"""
Property-based test for CommandProxy routing completeness.

Feature: v0.4.0-platform-extensions, Property 7: CommandProxy routing completeness

Uses Hypothesis to sample from all registered ActionDefs and verify that
_resolve_action(action.command) resolves to an ActionDef whose id matches
the original ActionDef.id.

**Validates: Requirements 17.4**
"""

from __future__ import annotations

from unittest.mock import MagicMock

from hypothesis import given, settings
from hypothesis import strategies as st

from ue_cli_tool.command_proxy import CommandProxy
from ue_cli_tool.registry import get_registry


# ---------------------------------------------------------------------------
# Strategies
# ---------------------------------------------------------------------------

def _all_action_ids() -> st.SearchStrategy[str]:
    """Strategy that samples from all registered action IDs."""
    registry = get_registry()
    all_ids = registry.all_ids
    assert len(all_ids) > 0, "Registry must have at least one registered action"
    return st.sampled_from(all_ids)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_mock_connection() -> MagicMock:
    """Create a mock PersistentUnrealConnection for CommandProxy."""
    conn = MagicMock()
    conn.send_raw_dict.return_value = {"success": True}
    return conn


# ---------------------------------------------------------------------------
# Property 7: CommandProxy routing completeness
# ---------------------------------------------------------------------------

@given(action_id=_all_action_ids())
@settings(max_examples=100)
def test_command_proxy_routing_completeness(action_id: str):
    """For any registered ActionDef, _resolve_action(action.command) should
    resolve to an ActionDef whose id matches the original ActionDef.id.

    Feature: v0.4.0-platform-extensions, Property 7: CommandProxy routing completeness

    **Validates: Requirements 17.4**
    """
    registry = get_registry()
    action = registry.get(action_id)
    assert action is not None, f"Action '{action_id}' should exist in registry"

    conn = _make_mock_connection()
    proxy = CommandProxy(conn)

    # Resolve using the C++ command name
    resolved = proxy._resolve_action(action.command)

    assert resolved is not None, (
        f"_resolve_action('{action.command}') returned None for action '{action_id}'. "
        f"Expected to resolve to ActionDef with id='{action_id}'."
    )
    assert resolved.id == action.id, (
        f"_resolve_action('{action.command}') resolved to '{resolved.id}', "
        f"expected '{action.id}'."
    )


# ===========================================================================
# Unit tests for CommandProxy
# Requirements: 17.1, 17.2, 17.3, 17.4
# ===========================================================================

import pytest


class TestMethodResolution:
    """Tests for CommandProxy method resolution strategies.

    **Validates: Requirements 17.1**
    """

    def test_direct_cpp_command_match(self):
        """Strategy 1: C++ command name direct match.

        'create_blueprint' is the C++ command for action 'blueprint.create'.
        _resolve_action should find it via get_by_command().
        """
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)
        registry = get_registry()

        action = registry.get("blueprint.create")
        assert action is not None
        assert action.command == "create_blueprint"

        resolved = proxy._resolve_action("create_blueprint")
        assert resolved is not None
        assert resolved.id == "blueprint.create"

    def test_first_underscore_to_dot_match(self):
        """Strategy 2: first underscore → dot.

        'blueprint_create' → 'blueprint.create' (first '_' becomes '.').
        This should resolve via registry.get('blueprint.create').
        """
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        resolved = proxy._resolve_action("blueprint_create")
        assert resolved is not None
        assert resolved.id == "blueprint.create"

    def test_progressive_dot_replacement_match(self):
        """Strategy 3: progressive dot replacement.

        'node_add_event' → tries 'node.add_event' (split at index 1).
        The registry has action id 'node.add_event', so it should resolve.
        """
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        resolved = proxy._resolve_action("node_add_event")
        assert resolved is not None
        assert resolved.id == "node.add_event"

    def test_getattr_dispatches_via_resolution(self):
        """Accessing an attribute on the proxy should resolve and return a callable."""
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        # 'create_blueprint' is a valid C++ command name (Strategy 1)
        method = proxy.create_blueprint
        assert callable(method)

    def test_getattr_first_underscore_dispatch(self):
        """Accessing 'blueprint_compile' should resolve to 'blueprint.compile'."""
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        method = proxy.blueprint_compile
        assert callable(method)


class TestCache:
    """Tests for CommandProxy method cache behavior.

    **Validates: Requirements 17.2**
    """

    def test_first_call_triggers_resolution_subsequent_uses_cache(self):
        """First attribute access triggers _resolve_action; subsequent accesses
        return the cached callable without re-resolving."""
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        # Cache should be empty initially
        assert "create_blueprint" not in proxy._method_cache

        # First access triggers resolution
        method1 = proxy.create_blueprint
        assert "create_blueprint" in proxy._method_cache

        # Second access returns the same cached object
        method2 = proxy.create_blueprint
        assert method1 is method2

    def test_cache_is_per_name(self):
        """Different method names get separate cache entries."""
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        _ = proxy.create_blueprint
        _ = proxy.compile_blueprint

        assert "create_blueprint" in proxy._method_cache
        assert "compile_blueprint" in proxy._method_cache


class TestErrorHandling:
    """Tests for CommandProxy error handling.

    **Validates: Requirements 17.3**
    """

    def test_nonexistent_method_raises_attribute_error(self):
        """Accessing a method name that doesn't match any action should raise
        AttributeError."""
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        with pytest.raises(AttributeError, match="No action found for"):
            _ = proxy.totally_nonexistent_command_xyz

    def test_underscore_prefix_raises_attribute_error(self):
        """Accessing an attribute starting with '_' should raise AttributeError
        immediately (private attribute convention)."""
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        with pytest.raises(AttributeError):
            _ = proxy._some_private_attr

    def test_double_underscore_prefix_raises_attribute_error(self):
        """Dunder-style names should also raise AttributeError."""
        conn = _make_mock_connection()
        proxy = CommandProxy(conn)

        with pytest.raises(AttributeError):
            _ = proxy.__hidden
