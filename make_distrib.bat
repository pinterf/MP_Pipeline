md distrib
cd distrib
md x86
md x86_xp
md x64
md x64_xp
cd ..

xcopy /y Output\Win32\release\MP_Pipeline.dll distrib\x86\
xcopy /y Output\Win32\release\MP_Pipeline.dll.slave.exe distrib\x86\
xcopy /y Output\Win32\release\MP_Pipeline.dll.win64.slave.exe distrib\x86\
xcopy /y Output\Win32\release\MP_Pipeline.dll.win64 distrib\x86\

xcopy /y Output\Win32\releaseXP\MP_Pipeline.dll distrib\x86_xp\
xcopy /y Output\Win32\releaseXP\MP_Pipeline.dll.slave.exe distrib\x86_xp\
xcopy /y Output\Win32\releaseXP\MP_Pipeline.dll.win64.slave.exe distrib\x86_xp\
xcopy /y Output\Win32\releaseXP\MP_Pipeline.dll.win64 distrib\x86_xp\

xcopy /y Output\x64\release\MP_Pipeline.dll distrib\x64\
xcopy /y Output\x64\release\MP_Pipeline.dll.slave.exe distrib\x64\
xcopy /y Output\x64\release\MP_Pipeline.dll.win32.slave.exe distrib\x64\
xcopy /y Output\x64\release\MP_Pipeline.dll.win32 distrib\x64\

xcopy /y Output\x64\releaseXP\MP_Pipeline.dll distrib\x64_xp\
xcopy /y Output\x64\releaseXP\MP_Pipeline.dll.slave.exe distrib\x64_xp\
xcopy /y Output\x64\releaseXP\MP_Pipeline.dll.win32.slave.exe distrib\x64_xp\
xcopy /y Output\x64\releaseXP\MP_Pipeline.dll.win32 distrib\x64_xp\

xcopy /y Documentation\MP_Pipeline_readme.avs distrib\
xcopy /y LICENSE distrib\
xcopy /y README.MD distrib\
