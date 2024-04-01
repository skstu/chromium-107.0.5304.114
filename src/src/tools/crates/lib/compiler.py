# python3
# Copyright 2021 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Provide abstractions around concepts from the Rust compiler.

This module provides abstractions around the compiler's targets (build
architectures), and mappings between those architectures and GN conditionals."""

from __future__ import annotations

from enum import Enum
import re


class ArchSet:
    """A set of compiler target architectures.

    This is used to track the output of `cargo` or `rustc` and match it against
    different architectures.
    """

    def __init__(self, initial: set[str]):
        for a in initial:
            assert a in _RUSTC_ARCH_TO_BUILD_CONDITION
        # Internally stored as a set of architecture strings.
        self._set = initial

    def add_archset(self, other: ArchSet) -> bool:
        """Makes `self` into the union of `self` and `other`.

        Returns if anything was added to the ArchSet."""
        if self._set.issuperset(other._set):
            return False
        self._set.update(other._set)
        return True

    def has_arch(self, arch: str) -> bool:
        """Whether the `ArchSet` contains `arch`."""
        return arch in self._set

    def as_strings(self) -> set[str]:
        """Returns `self` as a raw set of strings."""
        return self._set

    def __bool__(self) -> bool:
        """Whether the `ArchSet` is non-empty."""
        return bool(self._set)

    def __len__(self) -> int:
        """The number of architectures in the `ArchSet`."""
        return len(self._set)

    def __repr__(self) -> str:
        """A string representation of the `ArchSet`."""
        return "ArchSet({})".format(repr(self._set))

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, ArchSet):
            return NotImplemented
        """Whether `self` and `other` contain the same architectures."""
        return self._set == other._set

    def __and__(self, other: ArchSet) -> ArchSet:
        """An intersection of `self` and `other`.

        Returns a new `ArchSet` that contains only the architectures that are
        present in both `self` and `other`."""
        return ArchSet(initial=(self._set & other._set))

    @staticmethod
    def ALL() -> ArchSet:
        """All valid architectures."""
        return ArchSet({k for k in _RUSTC_ARCH_TO_BUILD_CONDITION.keys()})

    @staticmethod
    def ONE() -> ArchSet:
        """Arbitrary selection of a single architecture."""
        return ArchSet({"aarch64-apple-ios"})

    @staticmethod
    def EMPTY() -> ArchSet:
        """No architectures."""
        return ArchSet(set())


class BuildCondition(Enum):
    """Each value corresponds to a BUILD file condition that can be branched on.

    These flags currently store the GN condition statements directly, to easily
    convert from a `BuildCondition` to a GN if-statement.
    """
    # For Android builds, these come from `android_abi_target` values in
    # //build/config/android/abi.gni.
    ANDROID_X86 = "is_android && target_cpu == \"x86\"",
    ANDROID_X64 = "is_android && target_cpu == \"x64\"",
    ANDROID_ARM = "is_android && target_cpu == \"arm\"",
    ANDROID_ARM64 = "is_android && target_cpu == \"arm64\"",
    # Not supported by rustc but is in //build/config/android/abi.gni
    #   ANDROID_MIPS = "is_android && target_cpu == \"mipsel\"",
    # Not supported by rustc but is in //build/config/android/abi.gni
    #   ANDROID_MIPS64 = "is_android && target_cpu == \"mips64el\"",
    # For Fuchsia builds, these come from //build/config/rust.gni.
    FUCHSIA_ARM64 = "is_fuchsia && target_cpu == \"arm64\"",
    FUCHSIA_X64 = "is_fuchsia && target_cpu == \"x64\"",
    # For iOS builds, these come from //build/config/rust.gni.
    IOS_ARM64 = "is_ios && target_cpu == \"arm64\"",
    IOS_ARM = "is_ios && target_cpu == \"arm\"",
    IOS_X64 = "is_ios && target_cpu == \"x64\"",
    IOS_X86 = "is_ios && target_cpu == \"x86\"",
    WINDOWS_X86 = "is_win && target_cpu == \"x86\"",
    WINDOWS_X64 = "is_win && target_cpu == \"x64\"",
    LINUX_X86 = "(is_linux || is_chromeos) && target_cpu == \"x86\"",
    LINUX_X64 = "(is_linux || is_chromeos) && target_cpu == \"x64\"",
    MAC_X64 = "is_mac && target_cpu == \"x64\"",
    MAC_ARM64 = "is_mac && target_cpu == \"arm64\"",
    # Combinations generated by BuildConditionSet._optimize()
    ALL_ARM32 = "target_cpu == \"arm\"",
    ALL_ARM64 = "target_cpu == \"arm64\"",
    ALL_X64 = "target_cpu == \"x64\"",
    ALL_X86 = "target_cpu == \"x86\"",
    ALL_ANDROID = "is_android",
    ALL_FUCHSIA = "is_fuchsia",
    ALL_IOS = "is_ios",
    ALL_WINDOWS = "is_win",
    ALL_LINUX = "is_linux || is_chromeos",
    ALL_MAC = "is_mac",
    NOT_ANDROID = "!is_android",
    NOT_FUCHSIA = "!is_fuchsia",
    NOT_IOS = "!is_ios",
    NOT_WINDOWS = "!is_win",
    NOT_LINUX = "!(is_linux || is_chromeos)",
    NOT_MAC = "!is_mac",

    def gn_condition(self) -> str:
        """Gets the GN conditional text that represents the `BuildCondition`."""
        return self.value[0]


class BuildConditionSet:
    """A group of conditions for which the BUILD file can branch on.

    The conditions are each OR'd together, that is the set combines a group of
    conditions where any one of the conditions would be enough to satisfy the
    set.

    The group of conditions is built from an ArchSet, but provides a separate
    abstraction as it can be optimized to combine `BuildCondition`s, in order to
    cover multiple BUILD file conditions with fewer, more general conditions.

    An empty BuildConditionSet is never true, so a BUILD file output that would
    be conditional on such a set should be skipped entirely.
    """

    def __init__(self, arch_set: ArchSet):
        self.arch_set = arch_set

    def is_always_true(self):
        """Whether the set covers all possible BUILD file configurations."""
        return len(self.arch_set) == len(ArchSet.ALL())

    def inverted(self):
        inverse: set[str] = ArchSet.ALL().as_strings(
        ) - self.arch_set.as_strings()
        return BuildConditionSet(ArchSet(initial=inverse))

    def get_gn_conditions(self):
        """Generate the set of BUILD file conditions as text.

        Returns:
            A set of GN conditions (as strings) that should be evaluated.
            The result should be true if any of them is true.

            An empty set is returned to indicate there are no conditions.
        """
        # No arches are covered! We should not use this BuildConditionSet
        # to generate any output as it would always be `false`.
        assert self.arch_set, ("Generating BUILD rules for an empty "
                               "BuildConditionSet (which is never true).")

        if self.is_always_true():
            return []  # All archs are covered, so no conditions needed.

        modes = {
            _RUSTC_ARCH_TO_BUILD_CONDITION[a]
            for a in self.arch_set.as_strings()
        }
        return [m.gn_condition() for m in self._optimize(modes)]

    def _optimize(self, modes: set[BuildCondition]) -> set[BuildCondition]:
        """Combine `BuildCondition`s into a smaller, more general set.

        Args:
            modes: A set of BuildConditions to optimize.

        Returns:
            A smaller set of BuildConditions, if it's possible to optimize, or
            the original `modes` set.
        """

        def build_cond(arch: str) -> BuildCondition:
            return _RUSTC_ARCH_TO_BUILD_CONDITION[arch]

        def build_conds_matching(matching: str) -> set[BuildCondition]:
            return {
                build_cond(arch)
                for arch in _RUSTC_ARCH_TO_BUILD_CONDITION
                if re.search(matching, arch)
            }

        # Defines a set of modes we can collapse more verbose modes down into.
        # For each pair, if all of the modes in the 2nd position are present,
        # we can replace them all with the mode in the 1st position.
        os_combinations: list[tuple[BuildCondition, set[BuildCondition]]] = [
            (BuildCondition.ALL_IOS,
             build_conds_matching(_RUSTC_ARCH_MATCH_IOS)),
            (BuildCondition.ALL_WINDOWS,
             build_conds_matching(_RUSTC_ARCH_MATCH_WINDOWS)),
            (BuildCondition.ALL_LINUX,
             build_conds_matching(_RUSTC_ARCH_MATCH_LINUX)),
            (BuildCondition.ALL_MAC,
             build_conds_matching(_RUSTC_ARCH_MATCH_MAC)),
            (BuildCondition.ALL_ANDROID,
             build_conds_matching(_RUSTC_ARCH_MATCH_ANDROID)),
            (BuildCondition.ALL_FUCHSIA,
             build_conds_matching(_RUSTC_ARCH_MATCH_FUCHSIA)),
        ]
        os_merges: list[tuple[BuildCondition, set[BuildCondition]]] = [
            (BuildCondition.NOT_ANDROID, {
                BuildCondition.ALL_FUCHSIA, BuildCondition.ALL_IOS,
                BuildCondition.ALL_WINDOWS, BuildCondition.ALL_LINUX,
                BuildCondition.ALL_MAC
            }),
            (BuildCondition.NOT_FUCHSIA, {
                BuildCondition.ALL_ANDROID, BuildCondition.ALL_IOS,
                BuildCondition.ALL_WINDOWS, BuildCondition.ALL_LINUX,
                BuildCondition.ALL_MAC
            }),
            (BuildCondition.NOT_IOS, {
                BuildCondition.ALL_ANDROID, BuildCondition.ALL_FUCHSIA,
                BuildCondition.ALL_WINDOWS, BuildCondition.ALL_LINUX,
                BuildCondition.ALL_MAC
            }),
            (BuildCondition.NOT_WINDOWS, {
                BuildCondition.ALL_ANDROID, BuildCondition.ALL_FUCHSIA,
                BuildCondition.ALL_IOS, BuildCondition.ALL_LINUX,
                BuildCondition.ALL_MAC
            }),
            (BuildCondition.NOT_LINUX, {
                BuildCondition.ALL_ANDROID, BuildCondition.ALL_FUCHSIA,
                BuildCondition.ALL_IOS, BuildCondition.ALL_WINDOWS,
                BuildCondition.ALL_MAC
            }),
            (BuildCondition.NOT_MAC, {
                BuildCondition.ALL_ANDROID, BuildCondition.ALL_FUCHSIA,
                BuildCondition.ALL_IOS, BuildCondition.ALL_WINDOWS,
                BuildCondition.ALL_LINUX
            })
        ]
        cpu_combinations: list[tuple[BuildCondition, set[BuildCondition]]] = [
            (BuildCondition.ALL_X86,
             build_conds_matching(_RUSTC_ARCH_MATCH_X86)),
            (BuildCondition.ALL_X64,
             build_conds_matching(_RUSTC_ARCH_MATCH_X64)),
            (BuildCondition.ALL_ARM32,
             build_conds_matching(_RUSTC_ARCH_MATCH_ARM32)),
            (BuildCondition.ALL_ARM64,
             build_conds_matching(_RUSTC_ARCH_MATCH_ARM64)),
        ]

        to_remove: set[BuildCondition] = set()

        for (combined, all_individual) in os_combinations:
            if modes & all_individual == all_individual:
                modes.add(combined)
                to_remove.update(all_individual)

        for (combined, all_individual) in os_merges:
            if modes & all_individual == all_individual:
                modes.add(combined)
                to_remove.update(all_individual)

        for (combined, all_individual) in cpu_combinations:
            # Only add cpu-specific things if it would add something new, if the
            # individual archs are not already covered by combined
            # `BuildCondition`s.
            if all_individual & to_remove != all_individual:
                if modes & all_individual == all_individual:
                    modes.add(combined)
                    to_remove.update(all_individual)

        for r in to_remove:
            modes.remove(r)

        return modes

    def __bool__(self) -> bool:
        """Whether the BuildConditionSet has any conditions."""
        return bool(self.arch_set)

    def __repr__(self) -> str:
        """A string representation of a `BuildConditionSet`."""
        return "BuildConditionSet({})".format(repr(self.arch_set))

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, BuildConditionSet):
            return NotImplemented
        """Whether two sets cover the same BUILD file configurations."""
        return self.arch_set == other.arch_set

    @staticmethod
    def ALL() -> BuildConditionSet:
        """A set that covers all BUILD file configurations."""
        return BuildConditionSet(ArchSet.ALL())

    @staticmethod
    def EMPTY():
        """An empty set that represents never being true."""
        return BuildConditionSet(ArchSet.EMPTY())


# Internal representations used by ArchSet.
#
# This is a set of compiler targets known by rustc that we support. The full
# list is from `rustc --print target-list`.
#
# For each compiler target, we have a map to a BuildCondition which would
# represent the BUILD file condition that is true for the compiler target.
#
# NOTE: If this changes, then also update the `BuildConditionSet._optimize()``
# method that combines the `BuildCondition`s. Also update the matchers for sets
# of compiler targets, such as `_RUSTC_ARCH_MATCH_ANDROID`, below.
_RUSTC_ARCH_TO_BUILD_CONDITION = {
    "i686-linux-android": BuildCondition.ANDROID_X86,
    "x86_64-linux-android": BuildCondition.ANDROID_X64,
    "armv7-linux-androideabi": BuildCondition.ANDROID_ARM,
    "aarch64-linux-android": BuildCondition.ANDROID_ARM64,
    # Not supported by rustc but is in //build/config/android/abi.gni
    #   "mipsel-linux-android",
    # Not supported by rustc but is in //build/config/android/abi.gni.
    #   "mips64el-linux-android",
    "aarch64-fuchsia": BuildCondition.FUCHSIA_ARM64,
    "x86_64-fuchsia": BuildCondition.FUCHSIA_X64,
    "aarch64-apple-ios": BuildCondition.IOS_ARM64,
    "armv7-apple-ios": BuildCondition.IOS_ARM,
    "x86_64-apple-ios": BuildCondition.IOS_X64,
    "i386-apple-ios": BuildCondition.IOS_X86,
    # The winapi crate has dependencies that only exist on the "-gnu" flavour of
    # these windows targets. We would like to believe that we don't need them if
    # we are building with MSVC, or with clang which is pretending to be MSVC in
    # the Chromium build. If we get weird linking errors due to missing Windows
    # things in winapi, then we should probably change these to "-gnu".
    "i686-pc-windows-msvc": BuildCondition.WINDOWS_X86,
    "x86_64-pc-windows-msvc": BuildCondition.WINDOWS_X64,
    "i686-unknown-linux-gnu": BuildCondition.LINUX_X86,
    "x86_64-unknown-linux-gnu": BuildCondition.LINUX_X64,
    "x86_64-apple-darwin": BuildCondition.MAC_X64,
    "aarch64-apple-darwin": BuildCondition.MAC_ARM64,
}

# Regexs that will match all related architectures, and no unrelated ones.
_RUSTC_ARCH_MATCH_ANDROID = r"-android"
_RUSTC_ARCH_MATCH_FUCHSIA = r"-fuchsia"
_RUSTC_ARCH_MATCH_IOS = r"-apple-ios"
_RUSTC_ARCH_MATCH_WINDOWS = r"-windows"
_RUSTC_ARCH_MATCH_LINUX = r"-linux-gnu"
_RUSTC_ARCH_MATCH_MAC = r"-apple-darwin"
_RUSTC_ARCH_MATCH_X86 = r"^i[36]86-"
_RUSTC_ARCH_MATCH_X64 = r"^x86_64-"
_RUSTC_ARCH_MATCH_ARM32 = r"^armv7-"
_RUSTC_ARCH_MATCH_ARM64 = r"^aarch64-"