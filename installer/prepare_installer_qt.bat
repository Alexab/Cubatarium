mkdir InstallSourcesQt
rem set QtPath=C:\Qt
rem set QtVersion=5.12.2
rem set QtCompiler=msvc2017_64
rem set QtFullPath=%QtPath5\%QtVersion%\%QtCompiler%\bin\
rem set path=%path%;%QtFullPath%
xcopy "..\bin\Cubatarium.exe" InstallSourcesQt\bin\ /Y /D
xcopy "..\bin\Qt*.dll" InstallSourcesQt\bin\ /Y /D
xcopy "..\bin\opencv_*.dll" InstallSourcesQt\bin\ /Y /D
xcopy "..\bin\tbb.dll" InstallSourcesQt\bin\ /Y /D
xcopy "..\bin\tbb_debug.dll" InstallSourcesQt\bin\ /Y /D
xcopy "..\bin\config.json" InstallSourcesQt\bin\ /Y /D


xcopy "..\models\blocks\*.*" InstallSourcesQt\models\blocks\*.* /Y /E /D
xcopy "..\models\objects\*.*" InstallSourcesQt\models\objects\*.* /Y /E /D

xcopy "..\textures\blocks\*.*" InstallSourcesQt\textures\blocks\*.* /Y /E /D
xcopy "..\textures\items\*.*" InstallSourcesQt\textures\items\*.* /Y /E /D

xcopy "..\bin\plugins\platforms\*.dll" InstallSourcesQt\bin\plugins\platforms\*.dll /Y /E /D
rem xcopy "..\..\bin\plugins\platformthemes\*.dll" InstallSourcesQt\bin\plugins\platformthemes\*.dll /Y /E /D
rem xcopy "..\..\bin\plugins\printsupport\*.dll" InstallSourcesQt\bin\plugins\printsupport\*.dll /Y /E /D
rem xcopy "..\..\bin\plugins\styles\*.dll" InstallSourcesQt\bin\plugins\styles\*.dll /Y /E /D

"C:\Program Files (x86)\Actual Installer\actinst.exe" /S "CubatariumQt.aip"

