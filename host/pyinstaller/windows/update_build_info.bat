:: Helper script to update the build metadata in the release directory.
::
:: %1 output file

:: Caller provides output file path.
set out_file="%1"

:: Time and date.
echo %date% %time% > %out_file%

:: Python version.
echo: >> %out_file%
python --version >> %out_file%

:: Pip packges versions.
echo: >> %out_file%
pip freeze >> %out_file%


