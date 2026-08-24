"""
Remove or comment out console.log/error/warn/info/debug statements from HTML files
when not building from dev branch.
"""
Import("env")

import os
import re
import subprocess


def get_git_branch():
    """Get current git branch name, or 'unknown' if not available."""
    try:
        branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            stderr=subprocess.DEVNULL
        ).decode().strip()
        if branch.startswith("heads/"):
            branch = branch[6:]
        return branch
    except Exception:
        return "unknown"


def remove_console_statements(content):
    """
    Remove console.log, console.error, console.warn, console.info, console.debug statements.
    Uses a simple but effective approach: remove lines containing console statements.
    For multi-line statements, removes all lines until the semicolon is found.
    """
    console_methods = r'(log|error|warn|info|debug|trace|table|dir|dirxml|group|groupEnd|time|timeEnd|assert|clear|count|countReset|profile|profileEnd)'
    
    lines = content.split('\n')
    result_lines = []
    i = 0
    
    while i < len(lines):
        line = lines[i]
        
        # Check if this line contains a console statement
        if re.search(r'console\.' + console_methods + r'\s*\(', line):
            # Check if it's a complete statement on one line
            if ';' in line:
                # Single line statement - remove it entirely
                # But keep any code before the console statement on the same line
                match = re.search(r'console\.' + console_methods + r'\s*\([^;]*?\)\s*;', line)
                if match:
                    # Remove the console statement but keep rest of line
                    before = line[:match.start()].rstrip()
                    after = line[match.end():].strip()
                    if before or after:
                        result_lines.append((before + ' ' + after).strip())
                # Otherwise, just skip the line
                i += 1
                continue
            
            # Multi-line statement - find where it ends
            # Count parentheses to find matching closing paren
            paren_count = 0
            found_start = False
            j = i
            
            while j < len(lines):
                current_line = lines[j]
                
                # Find console statement start
                if not found_start:
                    match = re.search(r'console\.' + console_methods + r'\s*\(', current_line)
                    if match:
                        found_start = True
                        # Count opening paren
                        paren_count = 1
                        # Check rest of line for closing paren
                        remaining = current_line[match.end():]
                        for char in remaining:
                            if char == '(':
                                paren_count += 1
                            elif char == ')':
                                paren_count -= 1
                                if paren_count == 0:
                                    # Found end on same line
                                    if ';' in remaining:
                                        # Complete statement, skip this line
                                        i = j + 1
                                        break
                else:
                    # Already found start, look for closing paren
                    for char in current_line:
                        if char == '(':
                            paren_count += 1
                        elif char == ')':
                            paren_count -= 1
                            if paren_count == 0:
                                # Found end
                                if ';' in current_line:
                                    # Complete statement
                                    i = j + 1
                                    break
                
                if paren_count == 0:
                    break
                j += 1
            
            # Skip to after the statement
            if j < len(lines):
                i = j + 1
            else:
                i += 1
            continue
        else:
            # No console statement, keep the line
            result_lines.append(line)
            i += 1
    
    return '\n'.join(result_lines)


def process_html_files(*args, **kwargs):
    """Process HTML files to remove console statements if not on dev branch."""
    channel = os.environ.get("RELEASE_CHANNEL", "").strip()
    if channel:
        channel_norm = channel.lower()
        if channel_norm == "develop":
            print(f"[console-logs] Release channel is '{channel}', keeping all console statements")
            return
        print(f"[console-logs] Release channel is '{channel}', removing console statements")
    else:
        branch = get_git_branch()
        branch_norm = branch.lower()
        if branch_norm in ("dev", "develop"):
            print(f"[console-logs] Branch is '{branch}', keeping all console statements")
            return
        print(f"[console-logs] Branch is '{branch}', removing console statements")
    
    # PlatformIO copies files from data_dir to PROJECTDATA_DIR before buildfs
    # We work on PROJECTDATA_DIR (the build copy) to avoid modifying source files
    data_dir = env.subst("$PROJECTDATA_DIR")
    
    # Fallback to source directory if build directory doesn't exist yet
    # (shouldn't happen, but handle gracefully)
    if not os.path.exists(data_dir):
        project_dir = env.subst("$PROJECT_DIR")
        data_dir_source = os.path.join(project_dir, "data")
        if os.path.exists(data_dir_source):
            print(f"[console-logs] Warning: Build directory not found, using source: {data_dir_source}")
            print(f"[console-logs] Note: This may modify source files. Consider using 'pio run -t buildfs'")
            data_dir = data_dir_source
        else:
            print(f"[console-logs] Error: Data directory not found")
            print(f"[console-logs] Build dir: {env.subst('$PROJECTDATA_DIR')}")
            print(f"[console-logs] Source dir: {data_dir_source}")
            return
    
    print(f"[console-logs] Processing files in: {data_dir}")
    
    # Process all HTML files
    html_files = []
    for root, dirs, files in os.walk(data_dir):
        for file in files:
            if file.endswith('.html'):
                html_files.append(os.path.join(root, file))
    
    processed_count = 0
    for html_file in html_files:
        try:
            with open(html_file, 'r', encoding='utf-8') as f:
                content = f.read()
            
            original_content = content
            content = remove_console_statements(content)
            
            if content != original_content:
                with open(html_file, 'w', encoding='utf-8') as f:
                    f.write(content)
                processed_count += 1
                print(f"[console-logs] Processed: {os.path.basename(html_file)}")
        except Exception as e:
            print(f"[console-logs] Error processing {html_file}: {e}")
    
    if processed_count > 0:
        print(f"[console-logs] Removed console statements from {processed_count} file(s)")
    else:
        print(f"[console-logs] No console statements found to remove")


# Run before building filesystem
env.AddPreAction("buildfs", process_html_files)
