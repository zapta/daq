:: A Windows batch file to build a single file executable of the 
:: flasher app.

::set -e
::set -o xtrace

rd /s/q _dist
rd /s/q _build
rd /s/q _spec


mkdir _dist
mkdir _build
mkdir _spec

call update_build_info.bat ..\..\release\windows\flasher_build_info.txt

pyinstaller ..\..\src\flasher.py ^
  --paths ".." ^
  --clean ^
  --onefile ^
  --distpath _dist  ^
  --workpath _build ^
  --specpath _spec 


dir _dist

copy /b/y _dist\flasher.exe ..\..\release\windows\flasher.exe




