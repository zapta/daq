import shutil
import os
import logging

# logging.basicConfig(level=logging.INFO,
                    # format='%(levelname)-7s: %(message)s')
# logger = logging.getLogger(__name__)


def force_empty_dir(path):
  if os.path.exists(path):
    print(f"Deleting existing {path}.")
    shutil.rmtree(path, ignore_errors=True)
  assert not os.path.exists(path)
  print(f"Creating an empty directory {path}.")
  os.makedirs(path)
  assert os.path.exists(path)


def create_flatten_directory(src, dst):
  force_empty_dir(dst)
  # List files recursively.
  file_list = []
  for root, dirs, files in os.walk(src):
    for file in files:
      path = os.path.join(root, file);
      file_list.append(path.replace("\\", "/"))
  # Copy files      
  for src_file in file_list:
    basename = os.path.basename(src_file)
    dst_file = os.path.join(dst, basename).replace("\\", "/")
    print(f"Copying {src_file} -> {dst_file}")
    assert not os.path.exists(dst_file)
    shutil.copyfile(src_file, dst_file)
    assert os.path.exists(dst_file)
    
LIB="../platformio/lib"
assert os.path.exists(LIB)



force_empty_dir("flat_lib")
create_flatten_directory("Core", f"{LIB}/autogen_core")
create_flatten_directory("USB_DEVICE", f"{LIB}/autogen_usb")
create_flatten_directory("Middlewares", f"{LIB}/autogen_middlewares")




# dirpath, dirnames, filenames = os.walk(SRC)
# print(dirpath)
# print(dirnames)
# print(filenames)




