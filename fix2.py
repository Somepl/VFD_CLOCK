# -*- coding: utf-8 -*-
"""Fix corrupted Chinese text in all src files. Replace with English fallback text."""

import os
import re
import glob

SRC = "src"

# Known correct Chinese log messages (from serial output)
# Maps corrupted substring -> correct text
FIXES = {}

def read_file(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.read()

def write_file(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

def fix_file(fpath):
    content = read_file(fpath)
    original = content
    fname = os.path.basename(fpath)

    # Replace corrupted F() string literals with English fallback
    # Pattern: Log.println(F("...corrupted...")) or Serial.println(F("..."))
    lines = content.split('\n')
    new_lines = []
    changed = False

    for i, line in enumerate(lines):
        # Skip lines without F(
        if 'F(' not in line:
            new_lines.append(line)
            continue

        # Check if line has non-ASCII corrupted chars (U+FFFD or non-CJK non-ASCII)
        has_corrupt = False
        for c in line:
            if ord(c) == 0xFFFD:
                has_corrupt = True
                break

        if not has_corrupt:
            new_lines.append(line)
            continue

        # Extract module tag from the corrupted F() string
        m = re.search(r'F\("\[(\w+)\]', line)
        if m:
            tag = m.group(1)
            # Replace the entire F() argument with English fallback
            line = re.sub(
                r'(Log\.println\(F\(")\[?\w*\]?[^"]*("\))',
                r'\1[' + tag + r'] log\2',
                line
            )
            line = re.sub(
                r'(Log\.printf\("[^"]*)',
                r'\1',
                line
            )
            print(f"  {fname}:{i+1} Fixed [{tag}] string")
            changed = True
        
        new_lines.append(line)

    result = '\n'.join(new_lines)
    if result != content:
        write_file(fpath, result)
        return True
    return False

def main():
    for fname in sorted(os.listdir(SRC)):
        if not fname.endswith('.cpp'):
            continue
        fpath = os.path.join(SRC, fname)
        print(f"\nProcessing {fname}...")
        if fix_file(fpath):
            print(f"  -> Fixed")
        else:
            print(f"  -> No changes")

if __name__ == '__main__':
    main()
