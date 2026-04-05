"""
Tests for CLI parser 鈥?zero hand-maintained tables, all derived from ActionRegistry.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Python"))

from ue_cli_tool.registry import ActionRegistry, ActionDef, get_registry
from ue_cli_tool.cli_parser import CliParser, ParseResult, CommandDict

# 鈹€鈹€ Test fixture: real registry 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


@pytest.fixture
def registry() -> ActionRegistry:
    """Return the real ActionRegistry with all actions registered."""
    return get_registry()


@pytest.fixture
def parser(registry: ActionRegistry) -> CliParser:
    return CliParser(registry)


# 鈹€鈹€ Parsing basics 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestParseBasics:
    def test_empty_input(self, parser: CliParser):
        result = parser.parse("")
        assert result.commands == []
        assert result.errors == []

    def test_comments_only(self, parser: CliParser):
        result = parser.parse("# just a comment\n# another")
        assert result.commands == []
        assert result.errors == []

    def test_blank_lines_skipped(self, parser: CliParser):
        result = parser.parse("ping\n\n\nping")
        assert len(result.commands) == 2

    def test_single_command(self, parser: CliParser):
        result = parser.parse("ping")
        assert len(result.commands) == 1
        assert result.commands[0].command == "ping"

    def test_multiple_commands(self, parser: CliParser):
        result = parser.parse("ping\nping\nping")
        assert len(result.commands) == 3


# 鈹€鈹€ Context 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestContext:
    def test_context_target_injection(self, parser: CliParser):
        result = parser.parse("@BP_Enemy\ncompile_blueprint")
        cmd = result.commands[0]
        assert cmd.params.get("blueprint_name") == "BP_Enemy"

    def test_context_switch(self, parser: CliParser):
        result = parser.parse("@BP_A\ncompile_blueprint\n@BP_B\ncompile_blueprint")
        assert result.commands[0].params["blueprint_name"] == "BP_A"
        assert result.commands[1].params["blueprint_name"] == "BP_B"

    def test_no_context_no_injection(self, parser: CliParser):
        result = parser.parse("compile_blueprint")
        # Without @target, blueprint_name should NOT be in params
        assert "blueprint_name" not in result.commands[0].params

    def test_empty_context_error(self, parser: CliParser):
        result = parser.parse("@")
        assert len(result.errors) == 1
        assert "Empty @target" in result.errors[0]

    def test_context_applies_to_all_subsequent(self, parser: CliParser):
        script = "@BP_Enemy\nadd_blueprint_event_node ReceiveBeginPlay\nadd_blueprint_variable Health Float\ncompile_blueprint"
        result = parser.parse(script)
        for cmd in result.commands:
            assert cmd.params.get("blueprint_name") == "BP_Enemy"

    def test_material_context(self, parser: CliParser):
        result = parser.parse(
            "@M_Glow\nadd_material_expression MaterialExpressionVectorParameter BaseColor"
        )
        cmd = result.commands[0]
        assert cmd.params.get("material_name") == "M_Glow"

    def test_widget_context(self, parser: CliParser):
        result = parser.parse("@WBP_HUD\nadd_widget_component TextBlock MyText")
        cmd = result.commands[0]
        assert cmd.params.get("widget_name") == "WBP_HUD"


# 鈹€鈹€ Positional parameters 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestPositionalParams:
    def test_auto_map_to_required(self, parser: CliParser):
        result = parser.parse("create_blueprint BP_Test Actor")
        cmd = result.commands[0]
        assert cmd.params["name"] == "BP_Test"
        assert cmd.params["parent_class"] == "Actor"

    def test_context_excluded_slot_filling(self, parser: CliParser):
        result = parser.parse(
            "@BP_Enemy\nadd_component_to_blueprint StaticMeshComponent Mesh"
        )
        cmd = result.commands[0]
        assert cmd.params["blueprint_name"] == "BP_Enemy"
        assert cmd.params["component_type"] == "StaticMeshComponent"
        assert cmd.params["component_name"] == "Mesh"

    def test_three_positional(self, parser: CliParser):
        result = parser.parse("set_blueprint_property BP_P MaxHealth 100")
        cmd = result.commands[0]
        assert cmd.params["blueprint_name"] == "BP_P"
        assert cmd.params["property_name"] == "MaxHealth"
        assert cmd.params["property_value"] == 100

    def test_four_positional_connect_nodes(self, parser: CliParser):
        # connect_blueprint_nodes required: [blueprint_name, source_node_id, source_pin, target_node_id, target_pin]
        # Without @target, blueprint_name takes first positional slot
        result = parser.parse("@BP_Test\nconnect_blueprint_nodes SRC exec TGT then")
        cmd = result.commands[0]
        assert cmd.params["blueprint_name"] == "BP_Test"
        assert cmd.params["source_node_id"] == "SRC"
        assert cmd.params["source_pin"] == "exec"
        assert cmd.params["target_node_id"] == "TGT"
        assert cmd.params["target_pin"] == "then"


# 鈹€鈹€ --flag parameters 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestFlagParams:
    def test_flag_with_value(self, parser: CliParser):
        result = parser.parse("create_blueprint BP_Test Actor --path /Game/Weapons")
        cmd = result.commands[0]
        assert cmd.params["path"] == "/Game/Weapons"

    def test_flag_bool_true(self, parser: CliParser):
        result = parser.parse("add_blueprint_variable Health Float --is_exposed true")
        cmd = result.commands[0]
        assert cmd.params["is_exposed"] is True

    def test_flag_array(self, parser: CliParser):
        result = parser.parse(
            "add_component_to_blueprint StaticMeshComponent Mesh --location [0,0,100]"
        )
        cmd = result.commands[0]
        assert cmd.params["location"] == [0, 0, 100]

    def test_flag_no_value_is_true(self, parser: CliParser):
        result = parser.parse("some_command --verbose")
        cmd = result.commands[0]
        assert cmd.params["verbose"] is True

    def test_mixed_positional_and_flags(self, parser: CliParser):
        result = parser.parse(
            "@BP_E\nadd_component_to_blueprint StaticMeshComponent Mesh --location [0,0,100] --scale [2,2,2]"
        )
        cmd = result.commands[0]
        assert cmd.params["blueprint_name"] == "BP_E"
        assert cmd.params["component_type"] == "StaticMeshComponent"
        assert cmd.params["component_name"] == "Mesh"
        assert cmd.params["location"] == [0, 0, 100]
        assert cmd.params["scale"] == [2, 2, 2]


# 鈹€鈹€ Value coercion 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestCoercion:
    def test_bool_true(self, parser: CliParser):
        assert CliParser._coerce_value("true") is True

    def test_bool_false(self, parser: CliParser):
        assert CliParser._coerce_value("false") is False

    def test_bool_case_insensitive(self, parser: CliParser):
        assert CliParser._coerce_value("True") is True
        assert CliParser._coerce_value("FALSE") is False

    def test_int(self, parser: CliParser):
        assert CliParser._coerce_value("42") == 42

    def test_float(self, parser: CliParser):
        assert CliParser._coerce_value("3.14") == 3.14

    def test_json_array(self, parser: CliParser):
        assert CliParser._coerce_value("[1,2,3]") == [1, 2, 3]

    def test_json_object(self, parser: CliParser):
        assert CliParser._coerce_value('{"a":1}') == {"a": 1}

    def test_plain_string(self, parser: CliParser):
        assert CliParser._coerce_value("hello") == "hello"

    def test_path_string(self, parser: CliParser):
        assert CliParser._coerce_value("/Game/Blueprints") == "/Game/Blueprints"


# 鈹€鈹€ Quoted strings 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestQuotedStrings:
    def test_quoted_positional(self, parser: CliParser):
        # add_blueprint_comment required: [blueprint_name, comment_text]
        # With @target, blueprint_name is filled by context, so first positional 鈫?comment_text
        result = parser.parse(
            '@BP_Test\nadd_blueprint_comment "This is a multi word comment"'
        )
        cmd = result.commands[0]
        assert cmd.params.get("comment_text") == "This is a multi word comment"
        assert cmd.params.get("blueprint_name") == "BP_Test"

    def test_quoted_flag_value(self, parser: CliParser):
        result = parser.parse(
            'set_node_pin_default NODE1 InString --default_value "Hello World"'
        )
        cmd = result.commands[0]
        assert cmd.params["default_value"] == "Hello World"


# 鈹€鈹€ Batch conversion 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestBatchConversion:
    def test_to_batch_commands(self, parser: CliParser):
        result = parser.parse(
            "@BP_Test\ncreate_blueprint BP_Test Actor\ncompile_blueprint"
        )
        batch = parser.to_batch_commands(result)
        assert len(batch) == 2
        assert batch[0]["type"] == "create_blueprint"
        assert batch[0]["params"]["name"] == "BP_Test"
        assert batch[1]["type"] == "compile_blueprint"
        assert batch[1]["params"]["blueprint_name"] == "BP_Test"

    def test_empty_parse_result(self, parser: CliParser):
        result = parser.parse("")
        batch = parser.to_batch_commands(result)
        assert batch == []


# 鈹€鈹€ Edge cases 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestEdgeCases:
    def test_unknown_command_passthrough(self, parser: CliParser):
        result = parser.parse("totally_unknown_command arg1 arg2")
        cmd = result.commands[0]
        assert cmd.command == "totally_unknown_command"
        # Unknown command has no positional order 鈫?args not mapped
        assert cmd.params == {}

    def test_unknown_command_with_flags(self, parser: CliParser):
        result = parser.parse("unknown_cmd --foo bar --baz 42")
        cmd = result.commands[0]
        assert cmd.params["foo"] == "bar"
        assert cmd.params["baz"] == 42

    def test_excess_positional_args(self, parser: CliParser):
        # compile_blueprint only requires blueprint_name
        result = parser.parse("compile_blueprint BP_Test extra1 extra2")
        cmd = result.commands[0]
        assert cmd.params["blueprint_name"] == "BP_Test"
        # Excess positional args are silently ignored

    def test_flag_with_no_value_at_end(self, parser: CliParser):
        result = parser.parse("ping --debug")
        cmd = result.commands[0]
        assert cmd.params["debug"] is True


# 鈹€鈹€ Full scenario 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€


class TestFullScenario:
    def test_complete_blueprint_script(self, parser: CliParser):
        script = """
# Create an enemy character blueprint
@BP_EnemyCharacter
create_blueprint BP_EnemyCharacter Character --path /Game/Blueprints

add_component_to_blueprint CapsuleComponent Capsule
add_component_to_blueprint SkeletalMeshComponent CharMesh

add_blueprint_variable Health Float
add_blueprint_variable MaxHealth Float
add_blueprint_variable MoveSpeed Float

add_blueprint_event_node ReceiveBeginPlay
add_blueprint_event_node ReceiveTick

compile_blueprint
"""
        result = parser.parse(script)
        # 9 commands: create + 2 components + 3 variables + 2 events + compile
        # (create_blueprint has no blueprint_name property 鈫?no context injection)
        assert len(result.commands) == 9
        assert result.errors == []

        # Verify create
        assert result.commands[0].command == "create_blueprint"
        assert result.commands[0].params["name"] == "BP_EnemyCharacter"
        assert result.commands[0].params["parent_class"] == "Character"
        assert result.commands[0].params["path"] == "/Game/Blueprints"

        # Verify components have context injected
        assert result.commands[1].params["blueprint_name"] == "BP_EnemyCharacter"
        assert result.commands[1].params["component_type"] == "CapsuleComponent"
        assert result.commands[1].params["component_name"] == "Capsule"

        # Verify variables have context injected
        assert result.commands[3].params["blueprint_name"] == "BP_EnemyCharacter"
        assert result.commands[3].params["variable_name"] == "Health"
        assert result.commands[3].params["variable_type"] == "Float"

        # Verify events
        assert result.commands[6].command == "add_blueprint_event_node"
        assert result.commands[6].params["event_name"] == "ReceiveBeginPlay"

        # Verify compile (index 8 鈫?now 8)
        assert result.commands[8].command == "compile_blueprint"
        assert result.commands[8].params["blueprint_name"] == "BP_EnemyCharacter"

        # Batch conversion
        batch = parser.to_batch_commands(result)
        assert len(batch) == 9
        for item in batch:
            assert "type" in item
            assert "params" in item

    def test_multi_context_material_and_blueprint(self, parser: CliParser):
        script = """
# Blueprint
@BP_Player
add_component_to_blueprint StaticMeshComponent Body
compile_blueprint

# Material
@M_Glow
create_material --path /Game/Materials
add_material_expression MaterialExpressionVectorParameter BaseColor
compile_material
"""
        result = parser.parse(script)
        assert len(result.commands) == 5
        assert result.errors == []

        # Blueprint section
        assert result.commands[0].params["blueprint_name"] == "BP_Player"
        assert result.commands[1].params["blueprint_name"] == "BP_Player"

        # Material section
        assert result.commands[2].params["material_name"] == "M_Glow"
        assert result.commands[3].params["material_name"] == "M_Glow"
        assert result.commands[4].params["material_name"] == "M_Glow"

    def test_zero_handwritten_mapping(self, parser: CliParser):
        """Verify the parser has no hardcoded positional param tables."""
        # The parser should have no _POSITIONAL_PARAMS dict
        assert not hasattr(parser, "_POSITIONAL_PARAMS")
        # All positional orders come from registry
        registry = parser._registry
        action = registry.get_by_command("add_component_to_blueprint")
        assert action is not None
        order = parser._get_positional_order("add_component_to_blueprint")
        assert order == action.input_schema.get("required", [])
