@echo off
chcp 65001 >nul 2>&1
set CSC=C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\Roslyn\csc.exe
set OUT=bin\SuperSkillTool.exe
if not exist "bin" mkdir "bin"
"%CSC%" /nologo /utf8output /out:%OUT% /target:exe /platform:anycpu ^
  /reference:System.dll ^
  /reference:System.Core.dll ^
  /reference:System.Xml.dll ^
  /reference:System.Xml.Linq.dll ^
  /reference:System.Windows.Forms.dll ^
  /reference:System.Drawing.dll ^
  Program.cs ^
  Config\SkillDefinition.cs ^
  Config\SkillTemplate.cs ^
  Generators\ServerXmlGenerator.cs ^
  Generators\ServerStringXmlGenerator.cs ^
  Generators\DllJsonGenerator.cs ^
  Generators\ConfigJsonGenerator.cs ^
  Generators\SqlGenerator.cs ^
  Generators\ChecklistGenerator.cs ^
  Generators\HarepackerGuideGenerator.cs ^
  Generators\VerifyGenerator.cs ^
  Generators\SkillRemover.cs ^
  Util\PathConfig.cs ^
  Util\SimpleJson.cs ^
  Util\BackupHelper.cs ^
  Util\PngHelper.cs ^
  Util\SettingsManager.cs ^
  Interactive\InteractiveWizard.cs ^
  GUI\MainForm.cs
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo === BUILD OK: %OUT% ===
