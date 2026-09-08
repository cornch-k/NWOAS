@echo off
echo S123 LINK ROUNDTRIP AND READ-ONLY STATE
ver
echo DRIVES
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%d:\nul vol %%d:
echo S122 REPORT FROM USB OR SSD
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%d:\S122-REPORT.TXT type %%d:\S122-REPORT.TXT
echo USB MARKERS
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%d:\sources\install.swm dir %%d:\NWOAS-S12* /a
echo S117 SOURCE FILE SIZES
for %%d in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do if exist %%d:\S117SRC dir %%d:\S117SRC /a
echo S123 READ-ONLY INSPECTION COMPLETE
exit /b 0
