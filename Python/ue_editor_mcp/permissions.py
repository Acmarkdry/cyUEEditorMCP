"""
P9: Action permission policy — controls which actions AI can execute.

Policies:
  - auto:               Allow all actions (default)
  - confirm_destructive: Require confirm=true for destructive actions
  - readonly:           Block all write actions
"""

from __future__ import annotations
from enum import Enum
from typing import Optional


class PermissionLevel(str, Enum):
    AUTO = "auto"
    CONFIRM_DESTRUCTIVE = "confirm_destructive"
    READONLY = "readonly"


class PermissionPolicy:
    """Check whether an action is allowed to execute."""

    def __init__(self, level: PermissionLevel = PermissionLevel.AUTO):
        self._level = level

    @property
    def level(self) -> PermissionLevel:
        return self._level

    @level.setter
    def level(self, value: PermissionLevel | str):
        if isinstance(value, str):
            value = PermissionLevel(value)
        self._level = value

    def check(
        self,
        action_id: str,
        risk: str = "safe",
        capabilities: tuple = (),
        confirm: bool = False,
    ) -> Optional[dict]:
        """
        Check if the action is allowed.

        Returns None if allowed, or a dict with rejection details.
        """
        if self._level == PermissionLevel.AUTO:
            return None

        if self._level == PermissionLevel.READONLY:
            if "write" in capabilities:
                return {
                    "success": False,
                    "error": "PERMISSION_DENIED",
                    "error_type": "permission",
                    "action_id": action_id,
                    "policy": self._level.value,
                    "message": f"Readonly mode: write action '{action_id}' is blocked.",
                }

        if self._level == PermissionLevel.CONFIRM_DESTRUCTIVE:
            if risk == "destructive" and not confirm:
                return {
                    "success": False,
                    "error": "CONFIRMATION_REQUIRED",
                    "error_type": "permission",
                    "action_id": action_id,
                    "risk": risk,
                    "policy": self._level.value,
                    "message": (
                        f"Destructive action '{action_id}' requires confirmation. "
                        f"Re-call with confirm=true to proceed."
                    ),
                }

        return None
