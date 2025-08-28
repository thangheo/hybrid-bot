import os

def count_loc_in_file(file_path):
    loc = 0
    with open(file_path, 'r', encoding='utf-8', errors='ignore') as file:
        in_comment_block = False
        for line in file:
            stripped_line = line.strip()
            
            # Skip blank lines
            if not stripped_line:
                continue
            
            # Handle single-line comments
            if stripped_line.startswith('//'):
                continue
            
            # Handle multi-line comments (block comments)
            if '/*' in stripped_line:
                in_comment_block = True

            if '*/' in stripped_line:
                in_comment_block = False
                continue

            if in_comment_block:
                continue

            # If it's a valid line, count it
            loc += 1
    return loc

def count_loc_in_directory(directory, exclude_directories):
    total_loc = 0
    # for root, _, files in os.walk(directory):
    #     for file in files:
    #         if file.endswith('.cpp') or file.endswith('.h'):
    #             file_path = os.path.join(root, file)
    #             total_loc += count_loc_in_file(file_path)
    # return total_loc

    exclude_directories = exclude_directories.split(',')
    for root, _, files in os.walk(directory):
        if any(exclude_dir in root for exclude_dir in exclude_directories):
            continue
        for file in files:
            if file.endswith('.cpp') or file.endswith('.h'):
                file_path = os.path.join(root, file)
                total_loc += count_loc_in_file(file_path)
    return total_loc
if __name__ == "__main__":
    project_directory = input("Enter the path to the C++ project directory: ")
    exclude_directories = input("Enter the directories to exclude (separated by comma): ")
    total_loc = count_loc_in_directory(project_directory, exclude_directories)
    print(f"Total lines of code (excluding comments and blank lines): {total_loc}")
