"""Unit tests for the parse + lower pipeline against synthetic vk.xml fragments."""

from __future__ import annotations

import textwrap
import xml.etree.ElementTree as ET

from lsw_codegen import lower
from lsw_codegen.profile import Profile


def _make_xml(body: str) -> ET.Element:
    return ET.fromstring(textwrap.dedent(body))


def _profile(commands: list[str], ids: dict[str, int] | None = None) -> Profile:
    p = Profile()
    p.commands = commands
    p.command_ids = ids or {c: 0x10000 + i for i, c in enumerate(commands)}
    return p


def test_handle_extraction():
    xml = _make_xml(
        """\
        <registry>
          <types>
            <type category="handle"><type>VK_DEFINE_HANDLE</type>(<name>VkInstance</name>)</type>
            <type category="handle" parent="VkDevice">
              <type>VK_DEFINE_NON_DISPATCHABLE_HANDLE</type>(<name>VkBuffer</name>)
            </type>
          </types>
          <commands/>
        </registry>
        """
    )
    reg = lower.lower(xml, _profile([]))
    assert "VkInstance" in reg.handles
    assert reg.handles["VkInstance"].dispatchable is True
    assert reg.handles["VkBuffer"].dispatchable is False
    assert reg.handles["VkBuffer"].parent == "VkDevice"


def test_simple_async_command():
    xml = _make_xml(
        """\
        <registry>
          <types>
            <type category="handle"><type>VK_DEFINE_HANDLE</type>(<name>VkInstance</name>)</type>
          </types>
          <commands>
            <command>
              <proto><type>void</type> <name>vkDestroyInstance</name></proto>
              <param optional="true"><type>VkInstance</type> <name>instance</name></param>
              <param optional="true">const <type>VkAllocationCallbacks</type>* <name>pAllocator</name></param>
            </command>
          </commands>
        </registry>
        """
    )
    reg = lower.lower(xml, _profile(["vkDestroyInstance"]))
    cmd = reg.commands["vkDestroyInstance"]
    assert cmd.return_type.base == "void"
    assert cmd.sync_mode == "async"  # void return + no out-param
    assert len(cmd.params) == 2
    assert cmd.params[0].name == "instance"
    assert cmd.params[0].type.base == "VkInstance"
    assert cmd.params[0].type.pointer_depth == 0
    assert cmd.params[1].name == "pAllocator"
    assert cmd.params[1].type.is_const is True
    assert cmd.params[1].type.pointer_depth == 1


def test_two_call_enumeration_marks_sync():
    xml = _make_xml(
        """\
        <registry>
          <types>
            <type category="handle"><type>VK_DEFINE_HANDLE</type>(<name>VkInstance</name>)</type>
            <type category="handle"><type>VK_DEFINE_HANDLE</type>(<name>VkPhysicalDevice</name>)</type>
          </types>
          <commands>
            <command successcodes="VK_SUCCESS,VK_INCOMPLETE">
              <proto><type>VkResult</type> <name>vkEnumeratePhysicalDevices</name></proto>
              <param><type>VkInstance</type> <name>instance</name></param>
              <param optional="false,true"><type>uint32_t</type>* <name>pPhysicalDeviceCount</name></param>
              <param optional="true" len="pPhysicalDeviceCount"><type>VkPhysicalDevice</type>* <name>pPhysicalDevices</name></param>
            </command>
          </commands>
        </registry>
        """
    )
    reg = lower.lower(xml, _profile(["vkEnumeratePhysicalDevices"]))
    cmd = reg.commands["vkEnumeratePhysicalDevices"]
    assert cmd.return_type.base == "VkResult"
    assert cmd.sync_mode == "sync"  # non-void return
    assert cmd.params[1].len_expr is None
    assert cmd.params[1].optional_levels == (False, True)
    assert cmd.params[2].len_expr == "pPhysicalDeviceCount"
    assert cmd.success_codes == ("VK_SUCCESS", "VK_INCOMPLETE")


def test_unknown_command_raises():
    xml = _make_xml("<registry><commands/></registry>")
    try:
        lower.lower(xml, _profile(["vkNonExistent"]))
    except ValueError as e:
        assert "vkNonExistent" in str(e)
    else:
        raise AssertionError("expected ValueError")
