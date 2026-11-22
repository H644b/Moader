# SPDX-License-Identifier: GPL-2.0-only
param([Parameter(Mandatory=$true)][string]$Kit)
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$distros = @(& wsl.exe --list --quiet 2>$null | ForEach-Object { ($_.Replace([char]0, '')).Trim() } | Where-Object { $_ })
if ($distros.Count -eq 0) {
  [System.Windows.Forms.MessageBox]::Show('Install and open a WSL2 Linux distribution before using this backend kit. Native Windows tools are not supported for the Linux-backed methods.', 'Moader Setup') | Out-Null
  exit 1
}
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Moader Setup · choose WSL'
$form.Size = New-Object System.Drawing.Size(440,210)
$form.StartPosition = 'CenterScreen'
$form.Font = New-Object System.Drawing.Font('Segoe UI',10)
$label = New-Object System.Windows.Forms.Label
$label.Text = 'Choose the WSL2 distribution that will run Moader Setup:'
$label.AutoSize = $true
$label.Location = New-Object System.Drawing.Point(20,20)
$form.Controls.Add($label)
$list = New-Object System.Windows.Forms.ComboBox
$list.DropDownStyle = 'DropDownList'
$list.Location = New-Object System.Drawing.Point(20,60)
$list.Width = 390
[void]$list.Items.AddRange([object[]]$distros)
$list.SelectedIndex = 0
$form.Controls.Add($list)
$help = New-Object System.Windows.Forms.Label
$help.Text = 'Moader uses Linux Node and paths inside WSL. The kit is copied into that distribution.'
$help.Size = New-Object System.Drawing.Size(390,40)
$help.Location = New-Object System.Drawing.Point(20,98)
$form.Controls.Add($help)
$go = New-Object System.Windows.Forms.Button
$go.Text = 'Open Moader Setup'
$go.Location = New-Object System.Drawing.Point(240,140)
$go.Size = New-Object System.Drawing.Size(170,28)
$go.DialogResult = [System.Windows.Forms.DialogResult]::OK
$form.Controls.Add($go)
$form.AcceptButton = $go
if ($form.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 0 }
$distro = [string]$list.SelectedItem
$kernel = (& wsl.exe -d $distro -- uname -r).Trim()
if ($kernel -notmatch 'WSL2') {
  [System.Windows.Forms.MessageBox]::Show('Choose a WSL2 distribution. The selected distribution does not report a WSL2 kernel.', 'Moader Setup') | Out-Null
  exit 1
}
$linuxKit = (& wsl.exe -d $distro -- wslpath -u $Kit).Trim()
if (-not $linuxKit.StartsWith('/')) {
  [System.Windows.Forms.MessageBox]::Show('The selected WSL distribution could not locate the extracted kit.', 'Moader Setup') | Out-Null
  exit 1
}
& wsl.exe -d $distro -- /bin/sh "$linuxKit/setup/wsl-entry.sh" $linuxKit
exit $LASTEXITCODE
