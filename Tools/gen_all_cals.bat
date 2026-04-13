@echo off
setlocal
cd /d "%~dp0"

python write_cal.py --serial 10326 --sensitivity 0.220919 --cellCorrFactor 0.973379 -o 10326.cal
python write_cal.py --serial 10426 --sensitivity 0.220919 --cellCorrFactor 1.018568 -o 10426.cal
python write_cal.py --serial 10526 --sensitivity 0.220919 --cellCorrFactor 0.998026 -o 10526.cal
python write_cal.py --serial 10626 --sensitivity 0.220919 --cellCorrFactor 1.010026 -o 10626.cal

echo Done. Copy *.cal to SYSCAL partition (1:).
endlocal
