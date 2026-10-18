Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

[System.Windows.Forms.Application]::EnableVisualStyles()

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nativeBuild = Join-Path $repoRoot 'build\native-release'
$gameBuild = Join-Path $repoRoot 'build\rexglue-game'
$llvmBin = Join-Path $repoRoot 'tools\toolchain\llvm-mingw\llvm-mingw-20261006-ucrt-x86_64\bin'
$cameraTool = Join-Path $nativeBuild 'fnk_camera_test.exe'
$previewTool = Join-Path $nativeBuild 'fnk_pose_preview.exe'
$calibrationTool = Join-Path $nativeBuild 'fnk_calibration_test.exe'
$gameExecutable = Join-Path $gameBuild 'fruit_ninja_kinect_recomp.exe'
$gameData = Join-Path $repoRoot 'artifacts\extracted\fruit-ninja-kinect'
$poseModel = Join-Path $repoRoot 'tools\pose\models\movenet-singlepose-lightning.onnx'
$calibrationProfile = Join-Path $repoRoot 'artifacts\calibration\droidcam.toml'
$settingsDirectory = Join-Path $repoRoot 'state'
$settingsPath = Join-Path $settingsDirectory 'launcher-settings.json'

$env:Path = "$llvmBin;$nativeBuild;$gameBuild;$env:Path"

$script:previewProcess = $null
$script:calibrationProcess = $null
$script:gameProcess = $null

$background = [Drawing.Color]::FromArgb(24, 27, 31)
$panelColor = [Drawing.Color]::FromArgb(34, 39, 44)
$fieldColor = [Drawing.Color]::FromArgb(47, 53, 59)
$textColor = [Drawing.Color]::FromArgb(236, 239, 241)
$mutedColor = [Drawing.Color]::FromArgb(170, 180, 188)
$accentColor = [Drawing.Color]::FromArgb(111, 190, 74)
$accentDark = [Drawing.Color]::FromArgb(74, 145, 45)
$warningColor = [Drawing.Color]::FromArgb(244, 180, 60)

function New-Label([string]$text, [int]$x, [int]$y, [int]$width, [int]$height = 24) {
    $label = New-Object Windows.Forms.Label
    $label.Text = $text
    $label.Location = New-Object Drawing.Point($x, $y)
    $label.Size = New-Object Drawing.Size($width, $height)
    $label.ForeColor = $textColor
    $label.BackColor = [Drawing.Color]::Transparent
    $label.Font = New-Object Drawing.Font('Segoe UI', 9)
    return $label
}

function Set-Status([string]$text, [Drawing.Color]$color = $mutedColor) {
    $statusLabel.Text = $text
    $statusLabel.ForeColor = $color
}

function Get-SelectedCameraIndex {
    if ($cameraCombo.SelectedItem -eq $null) { return 0 }
    return [int]$cameraCombo.SelectedItem.Index
}

function Get-SelectedVideoMode {
    $parts = $resolutionCombo.SelectedItem.ToString().Split('x')
    return @([int]$parts[0], [int]$parts[1])
}

function Save-LauncherSettings {
    New-Item -ItemType Directory -Force -Path $settingsDirectory | Out-Null
    $settings = [ordered]@{
        camera = Get-SelectedCameraIndex
        resolution = $resolutionCombo.SelectedItem.ToString()
        fps = [int]$fpsCombo.SelectedItem
        physicalDistance = [double]$distanceControl.Value
        gameDepth = [double]$gameDepthControl.Value
        confidence = [double]$confidenceControl.Value
    }
    $settings | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding UTF8
}

function Load-LauncherSettings {
    if (-not (Test-Path -LiteralPath $settingsPath)) { return $null }
    try {
        return Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Get-ProfileValue([string]$key) {
    if (-not (Test-Path -LiteralPath $calibrationProfile)) { return $null }
    $match = Select-String -LiteralPath $calibrationProfile -Pattern "^$key\s*=\s*([0-9.]+)" |
        Select-Object -First 1
    if ($match -eq $null) { return $null }
    return [double]::Parse($match.Matches[0].Groups[1].Value,
        [Globalization.CultureInfo]::InvariantCulture)
}

function Update-CalibrationStatus {
    if (-not (Test-Path -LiteralPath $calibrationProfile)) {
        $calibrationStateLabel.Text = 'Ainda não existe um perfil de calibração.'
        $calibrationStateLabel.ForeColor = $warningColor
        return
    }
    $physical = Get-ProfileValue 'reference_depth_m'
    $virtual = Get-ProfileValue 'game_reference_depth_m'
    if ($virtual -eq $null) { $virtual = 2.0 }
    $calibrationStateLabel.Text = ('Perfil atual: distância física {0:N1} m → Kinect virtual {1:N1} m' -f $physical, $virtual)
    $calibrationStateLabel.ForeColor = $accentColor
}

function Refresh-Cameras([int]$preferredIndex = 0) {
    $cameraCombo.Items.Clear()
    if (-not (Test-Path -LiteralPath $cameraTool)) {
        Set-Status 'Ferramenta de câmera não encontrada. Compile o projeto nativo.' $warningColor
        return
    }
    try {
        $lines = & $cameraTool --list 2>&1
        foreach ($line in $lines) {
            if ($line -match '^\s*\[(\d+)\]\s+(.+)$') {
                [void]$cameraCombo.Items.Add([pscustomobject]@{
                    Index = [int]$Matches[1]
                    Name = $Matches[2].Trim()
                })
            }
        }
        for ($index = 0; $index -lt $cameraCombo.Items.Count; $index++) {
            if ([int]$cameraCombo.Items[$index].Index -eq $preferredIndex) {
                $cameraCombo.SelectedIndex = $index
                break
            }
        }
        if ($cameraCombo.SelectedIndex -lt 0 -and $cameraCombo.Items.Count -gt 0) {
            $cameraCombo.SelectedIndex = 0
        }
        if ($cameraCombo.Items.Count -eq 0) {
            Set-Status 'Nenhuma câmera foi encontrada pelo Media Foundation.' $warningColor
        } else {
            Set-Status ("{0} câmera(s) encontrada(s)." -f $cameraCombo.Items.Count) $accentColor
        }
    } catch {
        Set-Status ("Falha ao enumerar câmeras: {0}" -f $_.Exception.Message) $warningColor
    }
}

function New-ChildProcess([string]$fileName, [string]$arguments, [string]$workingDirectory,
                          [bool]$redirectOutput = $false) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $fileName
    $start.Arguments = $arguments
    $start.WorkingDirectory = $workingDirectory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.EnvironmentVariables['PATH'] = $env:Path
    if ($redirectOutput) {
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
    }
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    [void]$process.Start()
    return $process
}

$form = New-Object Windows.Forms.Form
$form.Text = 'Fruit Ninja Kinect Recomp — Launcher'
$form.StartPosition = 'CenterScreen'
$form.ClientSize = New-Object Drawing.Size(780, 610)
$form.MinimumSize = New-Object Drawing.Size(796, 649)
$form.BackColor = $background
$form.ForeColor = $textColor
$form.Font = New-Object Drawing.Font('Segoe UI', 9)
$form.MaximizeBox = $false

$header = New-Object Windows.Forms.Panel
$header.Location = New-Object Drawing.Point(0, 0)
$header.Size = New-Object Drawing.Size(780, 92)
$header.BackColor = [Drawing.Color]::FromArgb(18, 21, 24)
$form.Controls.Add($header)

$title = New-Label 'FRUIT NINJA KINECT RECOMP' 26 17 600 34
$title.Font = New-Object Drawing.Font('Segoe UI Semibold', 18)
$title.ForeColor = $accentColor
$header.Controls.Add($title)
$subtitle = New-Label 'Webcam → pose corporal → interface Kinect v1' 29 54 520 24
$subtitle.ForeColor = $mutedColor
$header.Controls.Add($subtitle)

$cameraPanel = New-Object Windows.Forms.Panel
$cameraPanel.Location = New-Object Drawing.Point(20, 110)
$cameraPanel.Size = New-Object Drawing.Size(740, 142)
$cameraPanel.BackColor = $panelColor
$form.Controls.Add($cameraPanel)

$sectionCamera = New-Label 'Câmera' 18 12 150 28
$sectionCamera.Font = New-Object Drawing.Font('Segoe UI Semibold', 12)
$cameraPanel.Controls.Add($sectionCamera)
$cameraPanel.Controls.Add((New-Label 'Dispositivo' 18 50 90))

$cameraCombo = New-Object Windows.Forms.ComboBox
$cameraCombo.Location = New-Object Drawing.Point(110, 47)
$cameraCombo.Size = New-Object Drawing.Size(455, 28)
$cameraCombo.DropDownStyle = 'DropDownList'
$cameraCombo.DisplayMember = 'Name'
$cameraCombo.BackColor = $fieldColor
$cameraCombo.ForeColor = $textColor
$cameraPanel.Controls.Add($cameraCombo)

$refreshButton = New-Object Windows.Forms.Button
$refreshButton.Text = 'Atualizar'
$refreshButton.Location = New-Object Drawing.Point(580, 45)
$refreshButton.Size = New-Object Drawing.Size(135, 30)
$cameraPanel.Controls.Add($refreshButton)

$cameraPanel.Controls.Add((New-Label 'Resolução' 18 96 90))
$resolutionCombo = New-Object Windows.Forms.ComboBox
$resolutionCombo.Location = New-Object Drawing.Point(110, 92)
$resolutionCombo.Size = New-Object Drawing.Size(145, 28)
$resolutionCombo.DropDownStyle = 'DropDownList'
[void]$resolutionCombo.Items.AddRange(@('640x480', '1280x720'))
$resolutionCombo.SelectedIndex = 0
$cameraPanel.Controls.Add($resolutionCombo)

$cameraPanel.Controls.Add((New-Label 'FPS' 286 96 44))
$fpsCombo = New-Object Windows.Forms.ComboBox
$fpsCombo.Location = New-Object Drawing.Point(330, 92)
$fpsCombo.Size = New-Object Drawing.Size(86, 28)
$fpsCombo.DropDownStyle = 'DropDownList'
[void]$fpsCombo.Items.AddRange(@('24', '30', '60'))
$fpsCombo.SelectedItem = '30'
$cameraPanel.Controls.Add($fpsCombo)

$previewButton = New-Object Windows.Forms.Button
$previewButton.Text = 'Ver captura e esqueleto'
$previewButton.Location = New-Object Drawing.Point(470, 90)
$previewButton.Size = New-Object Drawing.Size(245, 32)
$previewButton.BackColor = $fieldColor
$previewButton.ForeColor = $textColor
$previewButton.FlatStyle = 'Flat'
$previewButton.FlatAppearance.BorderColor = $accentColor
$cameraPanel.Controls.Add($previewButton)

$spacePanel = New-Object Windows.Forms.Panel
$spacePanel.Location = New-Object Drawing.Point(20, 270)
$spacePanel.Size = New-Object Drawing.Size(740, 185)
$spacePanel.BackColor = $panelColor
$form.Controls.Add($spacePanel)

$sectionSpace = New-Label 'Espaço de jogo' 18 12 200 28
$sectionSpace.Font = New-Object Drawing.Font('Segoe UI Semibold', 12)
$spacePanel.Controls.Add($sectionSpace)
$spacePanel.Controls.Add((New-Label 'Distância real até a câmera' 18 53 210))
$distanceControl = New-Object Windows.Forms.NumericUpDown
$distanceControl.Location = New-Object Drawing.Point(240, 49)
$distanceControl.Size = New-Object Drawing.Size(92, 28)
$distanceControl.DecimalPlaces = 1
$distanceControl.Increment = [decimal]0.1
$distanceControl.Minimum = [decimal]0.5
$distanceControl.Maximum = [decimal]4.0
$distanceControl.Value = [decimal]1.0
$spacePanel.Controls.Add($distanceControl)
$spacePanel.Controls.Add((New-Label 'metros' 340 53 55))

$spacePanel.Controls.Add((New-Label 'Distância virtual mostrada ao jogo' 18 91 250))
$gameDepthControl = New-Object Windows.Forms.NumericUpDown
$gameDepthControl.Location = New-Object Drawing.Point(276, 87)
$gameDepthControl.Size = New-Object Drawing.Size(92, 28)
$gameDepthControl.DecimalPlaces = 1
$gameDepthControl.Increment = [decimal]0.1
$gameDepthControl.Minimum = [decimal]1.3
$gameDepthControl.Maximum = [decimal]3.0
$gameDepthControl.Value = [decimal]2.0
$spacePanel.Controls.Add($gameDepthControl)
$spacePanel.Controls.Add((New-Label 'metros' 376 91 55))

$spacePanel.Controls.Add((New-Label 'Confiança mínima da pose' 455 53 185))
$confidenceControl = New-Object Windows.Forms.NumericUpDown
$confidenceControl.Location = New-Object Drawing.Point(643, 49)
$confidenceControl.Size = New-Object Drawing.Size(72, 28)
$confidenceControl.DecimalPlaces = 2
$confidenceControl.Increment = [decimal]0.05
$confidenceControl.Minimum = [decimal]0.10
$confidenceControl.Maximum = [decimal]0.80
$confidenceControl.Value = [decimal]0.25
$spacePanel.Controls.Add($confidenceControl)

$calibrationStateLabel = New-Label '' 18 128 470 38
$calibrationStateLabel.AutoEllipsis = $true
$spacePanel.Controls.Add($calibrationStateLabel)

$calibrateButton = New-Object Windows.Forms.Button
$calibrateButton.Text = 'Calibrar nesta distância'
$calibrateButton.Location = New-Object Drawing.Point(500, 126)
$calibrateButton.Size = New-Object Drawing.Size(215, 36)
$calibrateButton.BackColor = $fieldColor
$calibrateButton.ForeColor = $textColor
$calibrateButton.FlatStyle = 'Flat'
$calibrateButton.FlatAppearance.BorderColor = $warningColor
$spacePanel.Controls.Add($calibrateButton)

$hint = New-Label 'Ao mudar a distância física ou a posição do celular, faça uma nova calibração.' 26 467 710 24
$hint.ForeColor = $mutedColor
$form.Controls.Add($hint)

$statusLabel = New-Label 'Preparando launcher...' 26 500 710 46
$statusLabel.ForeColor = $mutedColor
$form.Controls.Add($statusLabel)

$playButton = New-Object Windows.Forms.Button
$playButton.Text = 'INICIAR FRUIT NINJA KINECT'
$playButton.Location = New-Object Drawing.Point(20, 550)
$playButton.Size = New-Object Drawing.Size(740, 44)
$playButton.BackColor = $accentDark
$playButton.ForeColor = [Drawing.Color]::White
$playButton.FlatStyle = 'Flat'
$playButton.FlatAppearance.BorderSize = 0
$playButton.Font = New-Object Drawing.Font('Segoe UI Semibold', 11)
$form.Controls.Add($playButton)

$saved = Load-LauncherSettings
if ($saved -ne $null) {
    if ($resolutionCombo.Items.Contains([string]$saved.resolution)) {
        $resolutionCombo.SelectedItem = [string]$saved.resolution
    }
    if ($fpsCombo.Items.Contains([string]$saved.fps)) {
        $fpsCombo.SelectedItem = [string]$saved.fps
    }
    if ($saved.physicalDistance -ge 0.5 -and $saved.physicalDistance -le 4.0) {
        $distanceControl.Value = [decimal]$saved.physicalDistance
    }
    if ($saved.gameDepth -ge 1.3 -and $saved.gameDepth -le 3.0) {
        $gameDepthControl.Value = [decimal]$saved.gameDepth
    }
    if ($saved.confidence -ge 0.1 -and $saved.confidence -le 0.8) {
        $confidenceControl.Value = [decimal]$saved.confidence
    }
}

$preferredCamera = if ($saved -ne $null) { [int]$saved.camera } else { 0 }
Refresh-Cameras $preferredCamera
Update-CalibrationStatus

$refreshButton.Add_Click({ Refresh-Cameras (Get-SelectedCameraIndex) })

$previewButton.Add_Click({
    if ($script:gameProcess -ne $null -and -not $script:gameProcess.HasExited) {
        [Windows.Forms.MessageBox]::Show('Feche o jogo antes de abrir a pré-visualização.',
            'Câmera em uso', 'OK', 'Information') | Out-Null
        return
    }
    if ($script:previewProcess -ne $null -and -not $script:previewProcess.HasExited) {
        $script:previewProcess.Kill()
        return
    }
    if (-not (Test-Path -LiteralPath $previewTool)) {
        [Windows.Forms.MessageBox]::Show('fnk_pose_preview.exe não foi encontrado. Compile o projeto nativo.',
            'Pré-visualização indisponível', 'OK', 'Error') | Out-Null
        return
    }
    Save-LauncherSettings
    $mode = Get-SelectedVideoMode
    $arguments = '--device {0} --width {1} --height {2} --fps {3} --threshold {4} --model "{5}"' -f `
        (Get-SelectedCameraIndex), $mode[0], $mode[1], [int]$fpsCombo.SelectedItem,
        ([double]$confidenceControl.Value).ToString([Globalization.CultureInfo]::InvariantCulture),
        $poseModel
    try {
        $script:previewProcess = New-ChildProcess $previewTool $arguments $repoRoot $false
        $previewButton.Text = 'Fechar pré-visualização'
        $calibrateButton.Enabled = $false
        $playButton.Enabled = $false
        Set-Status 'Pré-visualização aberta. Verde é o esqueleto; círculos são as mãos.' $accentColor
    } catch {
        Set-Status ("Não foi possível abrir a pré-visualização: {0}" -f $_.Exception.Message) $warningColor
    }
})

$calibrateButton.Add_Click({
    if ($script:gameProcess -ne $null -and -not $script:gameProcess.HasExited) {
        [Windows.Forms.MessageBox]::Show('Feche o jogo antes de calibrar a câmera.',
            'Câmera em uso', 'OK', 'Information') | Out-Null
        return
    }
    if ($script:previewProcess -ne $null -and -not $script:previewProcess.HasExited) {
        [Windows.Forms.MessageBox]::Show('Feche a pré-visualização antes de calibrar.',
            'Câmera em uso', 'OK', 'Information') | Out-Null
        return
    }
    if (-not (Test-Path -LiteralPath $calibrationTool)) {
        [Windows.Forms.MessageBox]::Show('fnk_calibration_test.exe não foi encontrado.',
            'Calibração indisponível', 'OK', 'Error') | Out-Null
        return
    }
    Save-LauncherSettings
    $distance = ([double]$distanceControl.Value).ToString([Globalization.CultureInfo]::InvariantCulture)
    $gameDepth = ([double]$gameDepthControl.Value).ToString([Globalization.CultureInfo]::InvariantCulture)
    $confidence = ([double]$confidenceControl.Value).ToString([Globalization.CultureInfo]::InvariantCulture)
    $arguments = '--device {0} --samples 45 --confidence {1} --depth {2} --game-depth {3} --timeout 60 --model "{4}" --output "{5}"' -f `
        (Get-SelectedCameraIndex), $confidence, $distance, $gameDepth, $poseModel,
        $calibrationProfile
    try {
        $script:calibrationProcess = New-ChildProcess $calibrationTool $arguments $repoRoot $true
        $calibrateButton.Text = 'Calibrando...'
        $calibrateButton.Enabled = $false
        $previewButton.Enabled = $false
        $playButton.Enabled = $false
        Set-Status 'Fique de frente, imóvel, com ombros e quadris visíveis durante a calibração.' $warningColor
    } catch {
        Set-Status ("Não foi possível iniciar a calibração: {0}" -f $_.Exception.Message) $warningColor
    }
})

$playButton.Add_Click({
    if ($script:gameProcess -ne $null -and -not $script:gameProcess.HasExited) {
        Set-Status 'O jogo já está em execução.' $warningColor
        return
    }
    if ($script:previewProcess -ne $null -and -not $script:previewProcess.HasExited) {
        [Windows.Forms.MessageBox]::Show('Feche a pré-visualização antes de iniciar o jogo.',
            'Câmera em uso', 'OK', 'Information') | Out-Null
        return
    }
    foreach ($required in @($gameExecutable, $gameData, $poseModel)) {
        if (-not (Test-Path -LiteralPath $required)) {
            [Windows.Forms.MessageBox]::Show("Arquivo necessário não encontrado:`n$required",
                'Não foi possível iniciar', 'OK', 'Error') | Out-Null
            return
        }
    }
    if (-not (Test-Path -LiteralPath $calibrationProfile)) {
        $answer = [Windows.Forms.MessageBox]::Show(
            'Ainda não existe uma calibração. Deseja iniciar mesmo assim com os valores padrão?',
            'Perfil de calibração ausente', 'YesNo', 'Warning')
        if ($answer -ne [Windows.Forms.DialogResult]::Yes) { return }
    } else {
        $profileDistance = Get-ProfileValue 'reference_depth_m'
        if ($profileDistance -ne $null -and
            [Math]::Abs($profileDistance - [double]$distanceControl.Value) -gt 0.05) {
            $answer = [Windows.Forms.MessageBox]::Show(
                ('O perfil foi calibrado em {0:N1} m, mas o launcher está em {1:N1} m. ' +
                 'É recomendável calibrar novamente. Iniciar mesmo assim?') -f
                    $profileDistance, [double]$distanceControl.Value,
                'Distância diferente da calibração', 'YesNo', 'Warning')
            if ($answer -ne [Windows.Forms.DialogResult]::Yes) { return }
        }
    }

    Save-LauncherSettings
    $mode = Get-SelectedVideoMode
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $gameExecutable
    $start.Arguments = '--gpu_plugin=xenos "--game_data_root={0}" --log_level=debug' -f $gameData
    $start.WorkingDirectory = $gameBuild
    $start.UseShellExecute = $false
    $start.EnvironmentVariables['PATH'] = $env:Path
    $start.EnvironmentVariables['FNK_CAMERA_DEVICE'] = (Get-SelectedCameraIndex).ToString()
    $start.EnvironmentVariables['FNK_CAMERA_WIDTH'] = $mode[0].ToString()
    $start.EnvironmentVariables['FNK_CAMERA_HEIGHT'] = $mode[1].ToString()
    $start.EnvironmentVariables['FNK_CAMERA_FPS'] = ([int]$fpsCombo.SelectedItem).ToString()
    $start.EnvironmentVariables['FNK_POSE_MODEL'] = $poseModel
    if (Test-Path -LiteralPath $calibrationProfile) {
        $start.EnvironmentVariables['FNK_CALIBRATION_PROFILE'] = $calibrationProfile
    }
    try {
        $script:gameProcess = New-Object Diagnostics.Process
        $script:gameProcess.StartInfo = $start
        [void]$script:gameProcess.Start()
        $playButton.Text = 'JOGO EM EXECUÇÃO'
        $playButton.Enabled = $false
        $previewButton.Enabled = $false
        $calibrateButton.Enabled = $false
        Set-Status ("Jogo iniciado com a câmera {0}." -f (Get-SelectedCameraIndex)) $accentColor
    } catch {
        Set-Status ("Falha ao iniciar o jogo: {0}" -f $_.Exception.Message) $warningColor
    }
})

$timer = New-Object Windows.Forms.Timer
$timer.Interval = 350
$timer.Add_Tick({
    if ($script:previewProcess -ne $null -and $script:previewProcess.HasExited) {
        $script:previewProcess.Dispose()
        $script:previewProcess = $null
        $previewButton.Text = 'Ver captura e esqueleto'
        $previewButton.Enabled = $true
        $calibrateButton.Enabled = $true
        $playButton.Enabled = $true
        Set-Status 'Pré-visualização encerrada; a câmera foi liberada.' $mutedColor
    }
    if ($script:calibrationProcess -ne $null -and $script:calibrationProcess.HasExited) {
        $exitCode = $script:calibrationProcess.ExitCode
        $output = $script:calibrationProcess.StandardOutput.ReadToEnd()
        $errorOutput = $script:calibrationProcess.StandardError.ReadToEnd()
        $script:calibrationProcess.Dispose()
        $script:calibrationProcess = $null
        $calibrateButton.Text = 'Calibrar nesta distância'
        $calibrateButton.Enabled = $true
        $previewButton.Enabled = $true
        $playButton.Enabled = $true
        Update-CalibrationStatus
        if ($exitCode -eq 0) {
            Set-Status 'Calibração concluída e salva. O jogo está pronto para iniciar.' $accentColor
        } else {
            $details = ($errorOutput + "`n" + $output).Trim()
            [Windows.Forms.MessageBox]::Show("A calibração não foi concluída.`n`n$details",
                'Falha na calibração', 'OK', 'Warning') | Out-Null
            Set-Status 'Calibração incompleta. Confira enquadramento e iluminação.' $warningColor
        }
    }
    if ($script:gameProcess -ne $null -and $script:gameProcess.HasExited) {
        $exitCode = $script:gameProcess.ExitCode
        $script:gameProcess.Dispose()
        $script:gameProcess = $null
        $playButton.Text = 'INICIAR FRUIT NINJA KINECT'
        $playButton.Enabled = $true
        $previewButton.Enabled = $true
        $calibrateButton.Enabled = $true
        if ($exitCode -eq 0) {
            Set-Status 'O jogo foi encerrado normalmente.' $mutedColor
        } else {
            Set-Status ("O jogo encerrou com código {0}. Consulte build\rexglue-game\logs." -f $exitCode) $warningColor
        }
    }
})
$timer.Start()

$form.Add_FormClosing({
    Save-LauncherSettings
    $timer.Stop()
    if ($script:previewProcess -ne $null -and -not $script:previewProcess.HasExited) {
        $script:previewProcess.Kill()
    }
    if ($script:calibrationProcess -ne $null -and -not $script:calibrationProcess.HasExited) {
        $script:calibrationProcess.Kill()
    }
})

[void]$form.ShowDialog()
