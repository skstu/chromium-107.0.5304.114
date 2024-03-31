#!/usr/bin/env vpython3
#
# Copyright 2020 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Helps generate enums.xml from ProductionSupportedFlagList.

This is only a best-effort attempt to generate enums.xml values for the
LoginCustomFlags enum. You need to verify this script picks the right string
value for the new features and double check the hash value by running
"AboutFlagsHistogramTest.*".
"""

from __future__ import print_function

import argparse
import os
import re
import hashlib
import ctypes
import xml.etree.ElementTree as ET
import logging
import sys

_CHROMIUM_SRC = os.path.join(os.path.dirname(__file__), os.pardir, os.pardir)
sys.path.append(os.path.join(_CHROMIUM_SRC, 'third_party', 'catapult', 'devil'))
from devil.utils import logging_common  # pylint: disable=wrong-import-position

_FLAG_LIST_FILE = os.path.join(_CHROMIUM_SRC, 'android_webview', 'java', 'src',
                               'org', 'chromium', 'android_webview', 'common',
                               'ProductionSupportedFlagList.java')
_ENUMS_XML_FILE = os.path.join(_CHROMIUM_SRC, 'tools', 'metrics', 'histograms',
                               'enums.xml')
_SCRIPT_PATH = '//android_webview/tools/generate_flag_labels.py'

# This script tries to guess the commandline switch/base::Feature name from the
# generated Java constant (assuming the constant name follows typical
# conventions), but sometimes the script generates the incorrect name.
# Once you update the KNOWN_MISTAKES dictionary, re-run the tool (either invoke
# this script directly or run the git-cl presubmit check) and it should generate
# the correct integer hash value for enums.xml.
#
# Keys are the names autogenerated by this script's logic, values are the actual
# string names used to define the base::Feature or commandline switch in C++
# code.
#
# pylint: disable=line-too-long
# yapf: disable
KNOWN_MISTAKES = {
    # 'AutogeneratedName': 'CorrectName',
    'WebViewAccelerateSmallCanvases': 'WebviewAccelerateSmallCanvases',
    'Canvas2dStaysGpuOnReadback': 'Canvas2dStaysGPUOnReadback',
    'EnableSharedImageForWebView': 'EnableSharedImageForWebview',
    'GmsCoreEmoji': 'GMSCoreEmoji',
    'KeyboardAccessoryPaymentVirtualCardFeature': 'IPH_KeyboardAccessoryPaymentVirtualCard',
    'CompositeBgColorAnimation': 'CompositeBGColorAnimation',
    'enable-http2-grease-settings': 'http2-grease-settings',
    'AvoidUnnecessaryBeforeUnloadCheckPostTask': 'AvoidUnnecessaryBeforeUnloadCheck',
    'AutofillShadowDom': 'AutofillShadowDOM',
    'HtmlParamElementUrlSupport': 'HTMLParamElementUrlSupport',
    'webview-mp-arch-fenced-frames': 'webview-mparch-fenced-frames',
    'ThrottleIntersectionObserverUma': 'ThrottleIntersectionObserverUMA',
    'RemoveCanceledTasksInTaskQueue': 'RemoveCanceledTasksInTaskQueue2',
    'CssOverflowForReplacedElements': 'CSSOverflowForReplacedElements',
    # The final entry should have a trailing comma for cleaner diffs. You may
    # remove the entry from this dictionary when it's time to delete the
    # corresponding base::Feature or commandline switch.
}
# yapf: enable
# pylint: enable=line-too-long


def GetSwitchId(label):
  """Generate a hash consistent with flags_ui::GetSwitchUMAId()."""
  digest = hashlib.md5(label.encode('utf-8')).hexdigest()
  first_eight_bytes = digest[:16]
  long_value = int(first_eight_bytes, 16)
  signed_32bit = ctypes.c_int(long_value).value
  return signed_32bit


def _Capitalize(value):
  value = value[0].upper() + value[1:].lower()
  if value == 'Webview':
    value = 'WebView'
  return value


def FormatName(name, convert_to_pascal_case):
  """Converts name to the correct format.

  If name is shouty-case (ex. 'SOME_NAME') like a Java constant, then:
    * it converts to pascal case (camel case, with the first letter capitalized)
      if convert_to_pascal_case == True (ex. 'SomeName')
    * it converts to hyphenates name and converts to lower case (ex.
      'some-name')
  raises
    ValueError if name contains quotation marks like a Java literal (ex.
      '"SomeName"')
  """
  has_quotes_re = re.compile(r'".*"')
  if has_quotes_re.match(name):
    raise ValueError('String literals are not supported (got {})'.format(name))
  name = re.sub(r'^[^.]+\.', '', name)
  sections = name.split('_')

  if convert_to_pascal_case:
    sections = [_Capitalize(section) for section in sections]
    return ''.join(sections)

  sections = [section.lower() for section in sections]
  return '-'.join(sections)


def ConvertNameIfNecessary(name):
  """Fixes any names which are known to be autogenerated incorrectly."""
  if name in KNOWN_MISTAKES.keys():
    return KNOWN_MISTAKES.get(name)
  return name


class Flag:
  """Simplified python equivalent of the Flag java class.

  See //android_webview/java/src/org/chromium/android_webview/common/Flag.java
  """

  def __init__(self, name, is_base_feature):
    self.name = name
    self.is_base_feature = is_base_feature


class EnumValue:
  """Represents a label/value pair consistent with enums.xml."""

  def __init__(self, label):
    self.label = label
    self.value = GetSwitchId(label)

  def ToXml(self):
    return '<int value="{value}" label="{label}"/>'.format(value=self.value,
                                                           label=self.label)


class UncheckedEnumValue(EnumValue):
  """Like an EnumValue, but value may not be correct."""

  def __init__(self, label, value):
    super().__init__(label)
    self.label = label
    self.value = value


def _GetExistingFlagEnums():
  with open(_ENUMS_XML_FILE) as f:
    root = ET.fromstring(f.read())
  all_enums = root.find('enums')
  login_custom_flags = all_enums.find('enum[@name="LoginCustomFlags"]')
  return {
      UncheckedEnumValue(item.get('label'), int(item.get('value')))
      for item in login_custom_flags
  }


def _RemoveDuplicates(enums, existing_labels):
  return [enum for enum in enums if enum.label not in existing_labels]


def ExtractFlagsFromJavaLines(lines):
  flags = []

  hanging_name_re = re.compile(
      r'(?:\s*Flag\.(?:baseFeature|commandLine)\()?(\S+),')
  pending_feature = False
  pending_commandline = False

  for line in lines:
    if 'baseFeature(' in line:
      pending_feature = True
    if 'commandLine(' in line:
      pending_commandline = True

    if pending_feature and pending_commandline:
      raise RuntimeError('Somehow this is both a baseFeature and commandLine '
                         'switch: ({})'.format(line))

    # This means we saw Flag.baseFeature() or Flag.commandLine() on this or a
    # previous line but haven't found that flag's name yet. Check if we can
    # find a name in this line.
    if pending_feature or pending_commandline:
      m = hanging_name_re.search(line)
      if m:
        name = m.group(1)
        try:
          formatted_name = FormatName(name, pending_feature)
          formatted_name = ConvertNameIfNecessary(formatted_name)
          flags.append(Flag(formatted_name, pending_feature))
          pending_feature = False
          pending_commandline = False
        except ValueError:
          logging.warning('String literals are not supported, skipping %s',
                          name)
  return flags


def _GetIncorrectWebViewEnums():
  """Find incorrect WebView EnumValue pairs and return the correct output

  This iterates through both ProductionSupportedFlagList and enums.xml and
  returns EnumValue pairs for:

  * Any ProductionSupportedFlagList entries which are already in enums.xml but
    with the wrong integer hash value
  * Any ProductionSupportedFlagList entries which are not yet in enums.xml

  Returns the tuple (enums_need_fixing, enums_to_add):
    enums_need_fixing: a set of (correct) EnumValues for any existing enums.xml
      values which are currently incorrect.
    enums_to_add: a set of EnumValues for anything which isn't already in
      enums.xml.
  """
  with open(_FLAG_LIST_FILE, 'r') as f:
    lines = f.readlines()
  flags = ExtractFlagsFromJavaLines(lines)

  enums = []
  for flag in flags:
    if flag.is_base_feature:
      enums.append(EnumValue(flag.name + ':enabled'))
      enums.append(EnumValue(flag.name + ':disabled'))
    else:
      enums.append(EnumValue(flag.name))
  production_supported_flag_labels = {enum.label for enum in enums}

  # Don't bother checking non-WebView flags. Folks modifying
  # ProductionSupportedFlagList shouldn't be responsible for updating
  # non-WebView flags.
  def is_webview_flag(unchecked_enum):
    return unchecked_enum.label in production_supported_flag_labels

  existing_flag_enums = set(filter(is_webview_flag, _GetExistingFlagEnums()))

  # Find the invalid enums and generate the corresponding correct enums
  def is_invalid_enum(unchecked_enum):
    correct_enum = EnumValue(unchecked_enum.label)
    return correct_enum.value != unchecked_enum.value

  enums_need_fixing = {
      EnumValue(unchecked_enum.label)
      for unchecked_enum in filter(is_invalid_enum, existing_flag_enums)
  }

  existing_labels = {enum.label for enum in existing_flag_enums}
  enums_to_add = _RemoveDuplicates(enums, existing_labels)
  return (enums_need_fixing, enums_to_add)


def _GenerateEnumFlagMessage(enums_need_fixing, enums_to_add):
  output = ''
  enums_path = '//tools/metrics/histograms/enums.xml'
  if enums_need_fixing:
    output += """\
It looks like some flags in enums.xml have the wrong 'int value'. This is
probably a mistake in {enums_path}. Please update these flags in enums.xml
to use the following (correct) int values:

""".format(enums_path=enums_path)
    output += '\n'.join(
        sorted(['  ' + enum.ToXml() for enum in enums_need_fixing]))

  if enums_to_add:
    output += """\
It looks like you added flags to ProductionSupportedFlagList but didn't yet add
the flags to {enums_path}. Please double-check that the following flag labels
are spelled correctly (case-sensitive). You can correct any spelling mistakes by
editing KNOWN_MISTAKES in {script_path}.
Once the spelling is correct, please run this tool again and add the following
to enums.xml:

""".format(enums_path=enums_path, script_path=_SCRIPT_PATH)
    output += '\n'.join(sorted(['  ' + enum.ToXml() for enum in enums_to_add]))

  output += """

You can run this check again by running the {script_path} tool.
""".format(script_path=_SCRIPT_PATH)

  return output


def CheckMissingWebViewEnums(input_api, output_api):
  """A presubmit check to find missing flag enums."""
  sources = input_api.AffectedSourceFiles(
      lambda affected_file: input_api.FilterSourceFile(
          affected_file,
          files_to_check=(r'.*\bProductionSupportedFlagList\.java$', )))
  if not sources:
    return []

  enums_need_fixing, enums_to_add = _GetIncorrectWebViewEnums()
  if not enums_need_fixing and not enums_to_add:
    # Return empty list to tell git-cl presubmit there were no errors.
    return []

  return [
      output_api.PresubmitPromptWarning(
          _GenerateEnumFlagMessage(enums_need_fixing, enums_to_add))
  ]


def main():
  parser = argparse.ArgumentParser()

  logging_common.AddLoggingArguments(parser)
  args = parser.parse_args()
  logging_common.InitializeLogging(args)

  enums_need_fixing, enums_to_add = _GetIncorrectWebViewEnums()
  if not enums_need_fixing and not enums_to_add:
    print('enums.xml is already up-to-date!')
    return

  print(_GenerateEnumFlagMessage(enums_need_fixing, enums_to_add))


if __name__ == '__main__':
  main()