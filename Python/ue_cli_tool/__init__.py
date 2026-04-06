# coding: utf-8
"""
UE CLI Tool —?CLI-native AI interface for controlling Unreal Engine Editor.

Provides a minimal 2-tool MCP server (ue_cli + ue_query) that accepts
CLI-style text commands, parsed via CliParser and executed over TCP.
"""

__version__ = "0.2.0"
__author__ = "zolnoor"

from .connection import PersistentUnrealConnection
from .server import main

__all__ = ["PersistentUnrealConnection", "main", "__version__"]
