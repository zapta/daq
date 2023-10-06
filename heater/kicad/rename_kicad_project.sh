#!/bin/bash -e

# A crude script to rename a kicad project.
# TODO: Supress error on empty directories.

name1="heater"
name2="heater"

#files=(main_board*)
#echo $files

# Rename files in current directory.
functheatern rename_children() {
  #echo -- Renaming in ${PWD}
  for f1 in $(find ${name1}* -maxdepth 0) ; do 
    f2="${f1/${name1}/${name2}}"
    echo mv ${f1} ${f2}
    mv ${f1} ${f2}
  done
}

# Rename files recursivly.
functheatern rename_recursive() {
  rename_children
  for f in *; do 
    if [[ -d ${f} ]]; then
      pushd ${f}
      rename_children
      popd
    fi
  done
}

functheatern fix_one_file() {
  f=$1
  #echo "Testing $f"
  #echo grep -c "$name1" ${f}
  set +e
  count=`grep -c "$name1" ${f}`
  set -$oldopt
  #echo $count
  if [[ $count != 0 ]]; then
    echo "Fixing $f"
    sed -i "s/${name1}/${name2}/g" $f
  fi
}

echo 'rm *cache*'
set +e
rm *cache*
set -$oldopt

rename_recursive
for f in $(find . -type f); do
  fix_one_file $f
done


