:: A Windows batch file to build a single file executable of the 
:: analyzer's app.

::set -e
::set -o xtrace

echo "Building for windows..."

rd /s/q _dist
rd /s/q _build
rd /s/q _spec


mkdir _dist
mkdir _build
mkdir _spec

pyinstaller ..\src/monitor.py ^
  --paths ".." ^
  --clean ^
  --onefile ^
  --distpath _dist  ^
  --workpath _build ^
  --specpath _spec 


dir _dist

;copy /b/y _dist\analyzer.exe ..\..\release\windows\analyzer.exe




