#!/usr/bin/env python3

"""Adds copyright notices to all the files that need them under the current
directory.

usage: add_copyright.py [--check]

With --check, prints out all the files missing the copyright notice and exits
with status 1 if any such files are found, 0 if none.
"""

import fileinput
import fnmatch
import os
import re
import sys

FILTERDIRS = ["extern", "build*", "assets", "config"]

COPYRIGHTRE = re.compile(r"Copyright \(C\) \d+(\-\d+)?  Cole Reynolds")
COPYRIGHTRE_EXACT = re.compile(r"Copyright \(C\) 2024-2026  Cole Reynolds")

PROGNAME = "kerix"
COPYRIGHT = "Copyright (C) 2025-2026  Cole Reynolds"

LICENSED = """
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>."""


def find(top, filename_glob, skip_glob_list):
    """Returns files in the tree rooted at top matching filename_glob but not
    in directories matching skip_glob_list."""

    file_list = []
    for path, dirs, files in os.walk(top):
        for glob in skip_glob_list:
            for match in fnmatch.filter(dirs, glob):
                dirs.remove(match)
        for filename in fnmatch.filter(files, filename_glob):
            file_list.append(os.path.join(path, filename))
    return file_list


def filtered_descendants(glob):
    """Returns glob-matching filenames under the current directory, but skips
    some irrelevant paths."""
    return find(".", glob, FILTERDIRS)


def skip(line, prefix="//"):
    """Returns true if line is all whitespace or shebang."""
    stripped = line.lstrip()
    return (
        stripped == ""
        or stripped.startswith("#!")
        or stripped.startswith(prefix + " " + PROGNAME)
    )


def comment(text, prefix):
    """Returns commented-out text.

    Each line of text will be prefixed by prefix and a space character.
    Any trailing whitespace will be trimmed.
    """
    accum = []
    for line in text.split("\n"):
        accum.append((prefix + " " + line).rstrip())
    return "\n".join(accum)


def insert_copyright(glob, comment_prefix):
    """Finds all glob-matching files under the current directory and inserts
    the copyright message into them unless they already have it or are empty.

    The copyright message goes into the first non-whitespace, non-
    shebang line in a file.  It is prefixed on each line by
    comment_prefix and a space.
    """
    progname = comment(PROGNAME, comment_prefix) + "\n"
    copyright = comment(COPYRIGHT, comment_prefix) + "\n"
    licensed = comment(LICENSED, comment_prefix) + "\n\n"
    for file in filtered_descendants(glob):
        has_copyright = False
        has_exact = False
        for line in fileinput.input(file, inplace=1):
            has_copyright = has_copyright or COPYRIGHTRE.search(line)
            has_exact = has_exact or COPYRIGHTRE_EXACT.search(line)
            if has_copyright and not has_exact:
                sys.stdout.write(copyright)
                has_exact = True
                continue
            if not has_copyright and not skip(line, comment_prefix):
                sys.stdout.write(progname)
                sys.stdout.write(copyright)
                sys.stdout.write(licensed)
                has_copyright = True
                has_exact = True
            sys.stdout.write(line)
        if not has_copyright:
            open(file, "a").write(progname + copyright + licensed)


def alert_if_no_copyright(glob, comment_prefix):
    """Prints names of all files missing a copyright message.

    Finds all glob-matching files under the current directory and checks if they
    contain the copyright message.  Prints the names of all the files that
    don't.

    Returns the total number of file names printed.
    """
    printed_count = 0
    for file in filtered_descendants(glob):
        has_copyright = False
        with open(file) as contents:
            for line in contents:
                if COPYRIGHTRE.search(line):
                    has_copyright = True
                    break
        if not has_copyright:
            print(file, " has no copyright message.")
            printed_count += 1
    return printed_count


def main():
    glob_comment_pairs = [("*.hpp", "//"), ("*.cpp", "//")]
    if "--check" in sys.argv:
        count = 0
        for pair in glob_comment_pairs:
            count += alert_if_no_copyright(*pair)
        sys.exit(count > 0)
    else:
        for pair in glob_comment_pairs:
            insert_copyright(*pair)


if __name__ == "__main__":
    main()
