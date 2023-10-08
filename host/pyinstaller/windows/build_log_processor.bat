:: A Windows batch file to build a single file executable of the 
:: log processor app.
::
:: See this bug regarding an error in pyqtgraph and the workaround of swapping
:: two lines:
:: https://github.com/pyinstaller/pyinstaller/issues/7991#issuecomment-1752032919
::
:: ~/AppData/Local/Programs/Python/Python312/lib/site-packages/pyqtgraph/canvas/CanvasManager.py


::set -e
::set -o xtrace

rd /s/q _dist
rd /s/q _build
rd /s/q _spec


mkdir _dist
mkdir _build
mkdir _spec

call update_build_info.bat ..\..\release\windows\log_processor_build_info.txt

pyinstaller ..\..\src\log_processor.py ^
  --paths ".." ^
  --clean ^
  --onefile ^
  --distpath _dist  ^
  --workpath _build ^
  --specpath _spec 


dir _dist

copy /b/y _dist\log_processor.exe ..\..\release\windows\log_processor.exe




